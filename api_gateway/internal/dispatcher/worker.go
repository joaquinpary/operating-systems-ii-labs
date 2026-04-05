package dispatcher

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/google/uuid"
	amqp "github.com/rabbitmq/amqp091-go"

	"lora-chads/api_gateway/internal/core_bridge"
	"lora-chads/api_gateway/internal/shipments"
	"lora-chads/api_gateway/pkg/rabbitmq"
)

type Dispatcher struct {
	pool    *core_bridge.Pool
	rabbit  *rabbitmq.Connection
	workers int

	mu             sync.RWMutex
	shipments      map[string]trackedShipment
	transactions   map[string]string
	startOnce      sync.Once
	wg             sync.WaitGroup
	startErr       error
	startCompleted chan struct{}
}

type trackedShipment struct {
	Request       shipments.ShipmentRequest
	TransactionID string
	Status        string
	CreatedAt     time.Time
}

func New(pool *core_bridge.Pool, rabbit *rabbitmq.Connection, workers int) *Dispatcher {
	if workers < 1 {
		workers = 1
	}

	return &Dispatcher{
		pool:           pool,
		rabbit:         rabbit,
		workers:        workers,
		shipments:      make(map[string]trackedShipment),
		transactions:   make(map[string]string),
		startCompleted: make(chan struct{}),
	}
}

func (dispatcher *Dispatcher) Start(ctx context.Context) error {
	dispatcher.startOnce.Do(func() {
		deliveries, err := dispatcher.rabbit.Consume(ctx, shipments.ShipmentsQueue)
		if err != nil {
			dispatcher.startErr = err
			close(dispatcher.startCompleted)
			return
		}

		for workerIndex := 0; workerIndex < dispatcher.workers; workerIndex++ {
			dispatcher.wg.Add(1)
			go dispatcher.consumeLoop(ctx, deliveries)
		}

		close(dispatcher.startCompleted)
	})

	<-dispatcher.startCompleted
	if dispatcher.startErr != nil {
		return dispatcher.startErr
	}

	<-ctx.Done()
	dispatcher.wg.Wait()
	return nil
}

func (dispatcher *Dispatcher) Stop() error {
	dispatcher.wg.Wait()
	return nil
}

func (dispatcher *Dispatcher) RegisterShipment(request shipments.ShipmentRequest) shipments.StatusResponse {
	prepared := request
	prepared.OriginID = strings.TrimSpace(prepared.OriginID)
	if strings.TrimSpace(prepared.ShipmentID) == "" {
		prepared.ShipmentID = uuid.NewString()
	}

	now := time.Now().UTC()

	dispatcher.mu.Lock()
	record, exists := dispatcher.shipments[prepared.ShipmentID]
	if !exists {
		record = trackedShipment{
			Request:   prepared,
			Status:    shipments.StatusPendingConfirmation,
			CreatedAt: now,
		}
		dispatcher.shipments[prepared.ShipmentID] = record
	} else {
		record.Request = prepared
		dispatcher.shipments[prepared.ShipmentID] = record
	}
	dispatcher.mu.Unlock()

	return toStatusResponse(record)
}

func (dispatcher *Dispatcher) HasShipment(shipmentID string) bool {
	shipmentID = strings.TrimSpace(shipmentID)
	if shipmentID == "" {
		return false
	}

	dispatcher.mu.RLock()
	_, exists := dispatcher.shipments[shipmentID]
	dispatcher.mu.RUnlock()
	return exists
}

func (dispatcher *Dispatcher) DeleteShipment(shipmentID string) {
	shipmentID = strings.TrimSpace(shipmentID)
	if shipmentID == "" {
		return
	}

	dispatcher.mu.Lock()
	record, exists := dispatcher.shipments[shipmentID]
	if exists {
		delete(dispatcher.shipments, shipmentID)
		if strings.TrimSpace(record.TransactionID) != "" {
			delete(dispatcher.transactions, record.TransactionID)
		}
	}
	dispatcher.mu.Unlock()
}

func (dispatcher *Dispatcher) Snapshot() []shipments.StatusResponse {
	dispatcher.mu.RLock()
	responses := make([]shipments.StatusResponse, 0, len(dispatcher.shipments))
	for _, record := range dispatcher.shipments {
		responses = append(responses, toStatusResponse(record))
	}
	dispatcher.mu.RUnlock()

	sort.Slice(responses, func(left, right int) bool {
		if responses[left].CreatedAt == responses[right].CreatedAt {
			return responses[left].ShipmentID < responses[right].ShipmentID
		}

		return responses[left].CreatedAt < responses[right].CreatedAt
	})

	return responses
}

func (dispatcher *Dispatcher) consumeLoop(ctx context.Context, deliveries <-chan amqp.Delivery) {
	defer dispatcher.wg.Done()

	for {
		select {
		case <-ctx.Done():
			return
		case delivery, ok := <-deliveries:
			if !ok {
				return
			}

			requeue, err := dispatcher.handleDelivery(ctx, delivery)
			if err != nil {
				_ = delivery.Nack(false, requeue)
				continue
			}

			_ = delivery.Ack(false)
		}
	}
}

func (dispatcher *Dispatcher) handleDelivery(ctx context.Context, delivery amqp.Delivery) (bool, error) {
	var message shipments.QueueMessage
	if err := json.Unmarshal(delivery.Body, &message); err != nil {
		return false, fmt.Errorf("decode queue message: %w", err)
	}

	switch message.Type {
	case shipments.CreateShipmentMessageType:
		var request shipments.ShipmentRequest
		if err := json.Unmarshal(message.Payload, &request); err != nil {
			return false, fmt.Errorf("decode shipment request: %w", err)
		}

		dispatcher.RegisterShipment(request)
		return false, nil

	case shipments.DispatchCommandType:
		var request shipments.DispatchRequest
		if err := json.Unmarshal(message.Payload, &request); err != nil {
			return false, fmt.Errorf("decode dispatch request: %w", err)
		}

		if err := dispatcher.dispatchShipment(ctx, request.ShipmentID); err != nil {
			if errors.Is(err, errShipmentNotFound) {
				return false, err
			}

			return true, err
		}

		return false, nil

	default:
		return false, fmt.Errorf("unsupported queue message type %q", message.Type)
	}
}

var errShipmentNotFound = errors.New("shipment not found")

func (dispatcher *Dispatcher) dispatchShipment(ctx context.Context, shipmentID string) error {
	record, err := dispatcher.getShipment(shipmentID)
	if err != nil {
		return err
	}

	if strings.TrimSpace(record.TransactionID) != "" {
		return nil
	}

	args, err := json.Marshal(record.Request)
	if err != nil {
		return fmt.Errorf("marshal dispatch request payload: %w", err)
	}

	response, err := dispatcher.pool.Send(ctx, core_bridge.Envelope{
		MsgType:    core_bridge.MsgGatewayCommand,
		SourceRole: core_bridge.RoleGateway,
		SourceID:   dispatcher.pool.SourceID(),
		TargetRole: core_bridge.RoleServer,
		TargetID:   "SERVER",
		Payload: core_bridge.Payload{
			Command: "create_new_order",
			Args:    string(args),
		},
	})
	if err != nil {
		return fmt.Errorf("send dispatch command to core: %w", err)
	}

	transactionID := strings.TrimSpace(response.Payload.TransactionID)
	if transactionID == "" {
		return fmt.Errorf("core response missing transaction_id")
	}

	dispatcher.mu.Lock()
	record.TransactionID = transactionID
	record.Status = shipments.StatusConfirmed
	dispatcher.shipments[record.Request.ShipmentID] = record
	dispatcher.transactions[transactionID] = record.Request.ShipmentID
	dispatcher.mu.Unlock()

	return nil
}

func (dispatcher *Dispatcher) getShipment(shipmentID string) (trackedShipment, error) {
	shipmentID = strings.TrimSpace(shipmentID)

	dispatcher.mu.RLock()
	record, exists := dispatcher.shipments[shipmentID]
	dispatcher.mu.RUnlock()
	if !exists {
		return trackedShipment{}, fmt.Errorf("%w: %s", errShipmentNotFound, shipmentID)
	}

	return record, nil
}

func toStatusResponse(record trackedShipment) shipments.StatusResponse {
	response := shipments.StatusResponse{
		ShipmentID:    record.Request.ShipmentID,
		TransactionID: record.TransactionID,
		OriginID:      record.Request.OriginID,
		Status:        record.Status,
		CreatedAt:     record.CreatedAt.Format(time.RFC3339),
		Items:         append([]shipments.ShipmentItem(nil), record.Request.Items...),
	}

	if response.Status == "" {
		response.Status = shipments.StatusPendingConfirmation
	}

	return response
}
