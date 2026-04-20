package dispatcher

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log"
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

// New builds a dispatcher that tracks shipments and consumes queue messages.
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

// Start opens the RabbitMQ consumers and runs the configured worker goroutines.
// create_shipment messages arrive via a fanout exchange (every replica gets a
// copy), while dispatch_command messages arrive via the shared queue (only one
// replica processes each dispatch).
func (dispatcher *Dispatcher) Start(ctx context.Context) error {
	dispatcher.startOnce.Do(func() {
		// Fanout: every replica receives all create_shipment messages.
		fanoutDeliveries, err := dispatcher.rabbit.ConsumeFanout(ctx, shipments.ShipmentsFanoutExchange)
		if err != nil {
			dispatcher.startErr = err
			close(dispatcher.startCompleted)
			return
		}

		// Shared queue: dispatch_command distributed among replicas.
		queueDeliveries, err := dispatcher.rabbit.Consume(ctx, shipments.ShipmentsQueue)
		if err != nil {
			dispatcher.startErr = err
			close(dispatcher.startCompleted)
			return
		}

		// Merge both delivery channels into one.
		merged := make(chan amqp.Delivery, 64)
		var mergeWg sync.WaitGroup
		mergeWg.Add(2)
		go func() {
			defer mergeWg.Done()
			for d := range fanoutDeliveries {
				merged <- d
			}
		}()
		go func() {
			defer mergeWg.Done()
			for d := range queueDeliveries {
				merged <- d
			}
		}()
		go func() {
			mergeWg.Wait()
			close(merged)
		}()

		for workerIndex := 0; workerIndex < dispatcher.workers; workerIndex++ {
			dispatcher.wg.Add(1)
			go dispatcher.consumeLoop(ctx, merged)
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

// Stop waits for active worker goroutines to finish.
func (dispatcher *Dispatcher) Stop() error {
	dispatcher.wg.Wait()
	return nil
}

// RegisterShipment records a shipment request and returns its tracked status.
func (dispatcher *Dispatcher) RegisterShipment(request shipments.ShipmentRequest) shipments.StatusResponse {
	prepared := request
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

// GetShipment returns a tracked shipment by shipment or transaction identifier.
func (dispatcher *Dispatcher) GetShipment(identifier string) (shipments.StatusResponse, bool) {
	identifier = strings.TrimSpace(identifier)
	if identifier == "" {
		return shipments.StatusResponse{}, false
	}

	dispatcher.mu.RLock()
	record, exists := dispatcher.lookupShipmentLocked(identifier)
	dispatcher.mu.RUnlock()
	if !exists {
		return shipments.StatusResponse{}, false
	}

	return toStatusResponse(record), true
}

// HasShipment reports whether a shipment is currently tracked.
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

// DeleteShipment removes a shipment and its transaction index from memory.
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

// MarkDispatched transitions a shipment to dispatched status using its
// transaction ID.  Called when the C++ core notifies the gateway that a HUB
// has confirmed dispatch.
func (dispatcher *Dispatcher) MarkDispatched(transactionID string) {
	transactionID = strings.TrimSpace(transactionID)
	if transactionID == "" {
		return
	}

	dispatcher.mu.Lock()
	defer dispatcher.mu.Unlock()

	shipmentID, exists := dispatcher.transactions[transactionID]
	if !exists {
		return
	}
	record, exists := dispatcher.shipments[shipmentID]
	if !exists {
		return
	}
	record.Status = shipments.StatusDispatched
	dispatcher.shipments[shipmentID] = record
	log.Printf("dispatcher: shipment %s marked dispatched (txn=%s)", shipmentID, transactionID)
}

// CancelShipment deletes a pending shipment owned by the current caller.
func (dispatcher *Dispatcher) CancelShipment(shipmentID string, callerUsername string) (shipments.StatusResponse, error) {
	shipmentID = strings.TrimSpace(shipmentID)
	callerUsername = strings.TrimSpace(callerUsername)

	dispatcher.mu.Lock()
	record, exists := dispatcher.shipments[shipmentID]
	if !exists {
		dispatcher.mu.Unlock()
		return shipments.StatusResponse{}, fmt.Errorf("%w: %s", errShipmentNotFound, shipmentID)
	}
	if record.Request.Owner != callerUsername {
		dispatcher.mu.Unlock()
		return shipments.StatusResponse{}, fmt.Errorf("%w: %s", errNotOwner, shipmentID)
	}
	if record.Status != shipments.StatusPendingConfirmation {
		dispatcher.mu.Unlock()
		return shipments.StatusResponse{}, fmt.Errorf("%w: %s", errShipmentAlreadyDispatched, shipmentID)
	}

	delete(dispatcher.shipments, shipmentID)
	if strings.TrimSpace(record.TransactionID) != "" {
		delete(dispatcher.transactions, record.TransactionID)
	}
	dispatcher.mu.Unlock()

	response := toStatusResponse(record)
	response.Status = shipments.StatusCancelled
	return response, nil
}

// Snapshot returns the full in-memory shipment view.
func (dispatcher *Dispatcher) Snapshot() []shipments.StatusResponse {
	return dispatcher.snapshot(func(trackedShipment) bool { return true })
}

// SnapshotForOwner returns the shipment view filtered by owner.
func (dispatcher *Dispatcher) SnapshotForOwner(owner string) []shipments.StatusResponse {
	owner = strings.TrimSpace(owner)
	return dispatcher.snapshot(func(record trackedShipment) bool {
		return record.Request.Owner == owner
	})
}

// snapshot builds a sorted shipment list for records matching include.
func (dispatcher *Dispatcher) snapshot(include func(trackedShipment) bool) []shipments.StatusResponse {
	dispatcher.mu.RLock()
	responses := make([]shipments.StatusResponse, 0, len(dispatcher.shipments))
	for _, record := range dispatcher.shipments {
		if !include(record) {
			continue
		}
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

// consumeLoop processes queue deliveries until the context or channel closes.
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
				log.Printf("dispatcher: handleDelivery error (requeue=%t): %v", requeue, err)
				_ = delivery.Nack(false, requeue)
				continue
			}

			_ = delivery.Ack(false)
		}
	}
}

// handleDelivery decodes one queue message and routes it to the dispatcher logic.
func (dispatcher *Dispatcher) handleDelivery(ctx context.Context, delivery amqp.Delivery) (bool, error) {
	var message shipments.QueueMessage
	if err := json.Unmarshal(delivery.Body, &message); err != nil {
		return false, fmt.Errorf("decode queue message: %w", err)
	}

	log.Printf("dispatcher: received queue message type=%s payload=%s", message.Type, string(message.Payload))

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
			return false, err
		}

		return false, nil

	case shipments.DispatchConfirmedMessageType:
		var confirmed struct {
			ShipmentID    string `json:"shipment_id"`
			TransactionID string `json:"transaction_id"`
		}
		if err := json.Unmarshal(message.Payload, &confirmed); err != nil {
			return false, fmt.Errorf("decode dispatch_confirmed: %w", err)
		}

		dispatcher.mu.Lock()
		record, exists := dispatcher.shipments[confirmed.ShipmentID]
		if exists && strings.TrimSpace(record.TransactionID) == "" {
			record.TransactionID = confirmed.TransactionID
			record.Status = shipments.StatusConfirmed
			dispatcher.shipments[confirmed.ShipmentID] = record
			dispatcher.transactions[confirmed.TransactionID] = confirmed.ShipmentID
		}
		dispatcher.mu.Unlock()
		return false, nil

	default:
		return false, fmt.Errorf("unsupported queue message type %q", message.Type)
	}
}

var errShipmentNotFound = errors.New("shipment not found")
var errShipmentAlreadyDispatched = errors.New("shipment already dispatched")
var errNotOwner = errors.New("shipment does not belong to current user")

// dispatchShipment sends the create order command to the C++ core.
func (dispatcher *Dispatcher) dispatchShipment(ctx context.Context, shipmentID string) error {
	record, err := dispatcher.getShipment(shipmentID)
	if err != nil {
		return err
	}

	if strings.TrimSpace(record.TransactionID) != "" {
		return nil
	}

	items := make([]core_bridge.Item, len(record.Request.Items))
	for i, si := range record.Request.Items {
		items[i] = core_bridge.Item{
			ItemID:   si.ItemID,
			ItemName: si.ItemName,
			Quantity: si.Quantity,
		}
	}

	response, err := dispatcher.pool.Command(ctx, "create_new_order", core_bridge.Payload{
		Items: items,
	})
	if err != nil {
		return fmt.Errorf("send dispatch command to core: %w", err)
	}

	log.Printf("dispatcher: dispatch response status=%s data=%s", response.Payload.Status, string(response.Payload.Data))

	if response.Payload.Status != "ok" {
		msg := "dispatch failed"
		if len(response.Payload.Data) > 0 {
			var d struct {
				Message string `json:"message"`
			}
			if json.Unmarshal(response.Payload.Data, &d) == nil && d.Message != "" {
				msg = d.Message
			}
		}
		return fmt.Errorf("core rejected dispatch: %s", msg)
	}

	var data struct {
		Status        string `json:"status"`
		DispatchHubID string `json:"dispatch_hub_id"`
		TransactionID int    `json:"transaction_id"`
	}
	if len(response.Payload.Data) > 0 {
		if err := json.Unmarshal(response.Payload.Data, &data); err != nil {
			return fmt.Errorf("decode dispatch response data: %w", err)
		}
	}

	if data.TransactionID == 0 {
		return fmt.Errorf("core response missing transaction_id")
	}

	transactionID := fmt.Sprintf("%d", data.TransactionID)

	// Mark confirmed immediately — the dispatch was accepted by the core.
	// The core sub-status (assigned/pending) tells us whether a HUB was found,
	// but from the gateway's perspective the shipment is confirmed.
	dispatcher.mu.Lock()
	record.TransactionID = transactionID
	record.Status = shipments.StatusConfirmed
	dispatcher.shipments[record.Request.ShipmentID] = record
	dispatcher.transactions[transactionID] = record.Request.ShipmentID
	dispatcher.mu.Unlock()

	log.Printf("dispatcher: shipment %s confirmed (core_status=%s), transaction_id=%s hub=%s",
		record.Request.ShipmentID, data.Status, transactionID, data.DispatchHubID)

	// Broadcast the result to all replicas so they update their in-memory state.
	dispatcher.broadcastDispatchConfirmed(ctx, record.Request.ShipmentID, transactionID)

	return nil
}

// broadcastDispatchConfirmed publishes the dispatch result to the fanout
// exchange so every replica updates its in-memory state.
func (dispatcher *Dispatcher) broadcastDispatchConfirmed(ctx context.Context, shipmentID, transactionID string) {
	payload, err := json.Marshal(struct {
		ShipmentID    string `json:"shipment_id"`
		TransactionID string `json:"transaction_id"`
	}{
		ShipmentID:    shipmentID,
		TransactionID: transactionID,
	})
	if err != nil {
		log.Printf("dispatcher: marshal dispatch_confirmed: %v", err)
		return
	}

	envelope, err := json.Marshal(shipments.QueueMessage{
		Type:    shipments.DispatchConfirmedMessageType,
		Payload: payload,
	})
	if err != nil {
		log.Printf("dispatcher: marshal dispatch_confirmed envelope: %v", err)
		return
	}

	if err := dispatcher.rabbit.PublishFanout(ctx, shipments.ShipmentsFanoutExchange, envelope); err != nil {
		log.Printf("dispatcher: publish dispatch_confirmed: %v", err)
	}
}

// getShipment resolves one tracked shipment or returns a not-found error.
func (dispatcher *Dispatcher) getShipment(shipmentID string) (trackedShipment, error) {
	shipmentID = strings.TrimSpace(shipmentID)

	dispatcher.mu.RLock()
	record, exists := dispatcher.lookupShipmentLocked(shipmentID)
	dispatcher.mu.RUnlock()
	if !exists {
		return trackedShipment{}, fmt.Errorf("%w: %s", errShipmentNotFound, shipmentID)
	}

	return record, nil
}

// lookupShipmentLocked searches both shipment and transaction indexes.
func (dispatcher *Dispatcher) lookupShipmentLocked(identifier string) (trackedShipment, bool) {
	if record, exists := dispatcher.shipments[identifier]; exists {
		return record, true
	}

	shipmentID, exists := dispatcher.transactions[identifier]
	if !exists {
		return trackedShipment{}, false
	}

	record, exists := dispatcher.shipments[shipmentID]
	return record, exists
}

// toStatusResponse converts an in-memory tracked shipment into an API response.
func toStatusResponse(record trackedShipment) shipments.StatusResponse {
	response := shipments.StatusResponse{
		ShipmentID:    record.Request.ShipmentID,
		TransactionID: record.TransactionID,
		Owner:         record.Request.Owner,
		Status:        record.Status,
		CreatedAt:     record.CreatedAt.Format(time.RFC3339),
		Items:         append([]shipments.ShipmentItem(nil), record.Request.Items...),
	}

	if response.Status == "" {
		response.Status = shipments.StatusPendingConfirmation
	}

	return response
}
