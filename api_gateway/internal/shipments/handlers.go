package shipments

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"strings"

	"github.com/gofiber/fiber/v2"

	"lora-chads/api_gateway/internal/core_bridge"
)

type statusQuerier interface {
	Query(ctx context.Context, shipmentID string) (core_bridge.Message, error)
}

type publisher interface {
	Publish(ctx context.Context, queue string, body []byte) error
}

type shipmentTracker interface {
	RegisterShipment(request ShipmentRequest) StatusResponse
	DeleteShipment(shipmentID string)
	HasShipment(shipmentID string) bool
	Snapshot() []StatusResponse
}

type Handler struct {
	pool      statusQuerier
	publisher publisher
	tracker   shipmentTracker
}

func NewHandler(pool statusQuerier, publisher publisher, tracker shipmentTracker) *Handler {
	return &Handler{
		pool:      pool,
		publisher: publisher,
		tracker:   tracker,
	}
}

func (handler *Handler) CreateShipment(ctx *fiber.Ctx) error {
	var request ShipmentRequest
	if err := ctx.BodyParser(&request); err != nil {
		return writeError(ctx, fiber.StatusBadRequest, "invalid shipment request body")
	}

	if err := validateShipmentRequest(request); err != nil {
		return writeError(ctx, fiber.StatusBadRequest, err.Error())
	}

	status := handler.tracker.RegisterShipment(request)
	request.ShipmentID = status.ShipmentID

	if err := handler.publish(ctx.UserContext(), CreateShipmentMessageType, request); err != nil {
		handler.tracker.DeleteShipment(request.ShipmentID)
		return writeError(ctx, fiber.StatusInternalServerError, "failed to queue shipment request")
	}

	return ctx.Status(fiber.StatusAccepted).JSON(ShipmentResponse{
		ShipmentID: request.ShipmentID,
		Status:     StatusPendingConfirmation,
	})
}

func (handler *Handler) Dispatch(ctx *fiber.Ctx) error {
	var request DispatchRequest
	if err := ctx.BodyParser(&request); err != nil {
		return writeError(ctx, fiber.StatusBadRequest, "invalid dispatch request body")
	}

	if err := validateDispatchRequest(request); err != nil {
		return writeError(ctx, fiber.StatusBadRequest, err.Error())
	}

	if !handler.tracker.HasShipment(request.ShipmentID) {
		return writeError(ctx, fiber.StatusNotFound, "shipment not found")
	}

	if err := handler.publish(ctx.UserContext(), DispatchCommandType, request); err != nil {
		return writeError(ctx, fiber.StatusInternalServerError, "failed to queue dispatch command")
	}

	return ctx.Status(fiber.StatusAccepted).JSON(ShipmentResponse{
		ShipmentID: request.ShipmentID,
		Status:     StatusPendingConfirmation,
	})
}

func (handler *Handler) GetAllStatuses(ctx *fiber.Ctx) error {
	return ctx.Status(fiber.StatusOK).JSON(handler.tracker.Snapshot())
}

func (handler *Handler) GetStatus(ctx *fiber.Ctx) error {
	transactionID := strings.TrimSpace(ctx.Params("id"))
	if transactionID == "" {
		return writeError(ctx, fiber.StatusBadRequest, "shipment id is required")
	}

	message, err := handler.pool.Query(ctx.UserContext(), transactionID)
	if err != nil {
		return writeError(ctx, fiber.StatusInternalServerError, "failed to query shipment status")
	}

	response := StatusResponse{
		TransactionID: transactionID,
		Status:        "unknown",
	}

	if len(message.Payload) > 0 {
		if err := json.Unmarshal(message.Payload, &response); err != nil {
			return writeError(ctx, fiber.StatusInternalServerError, "failed to decode status response")
		}
	}

	if response.TransactionID == "" {
		response.TransactionID = transactionID
	}

	if response.Status == "" {
		response.Status = "unknown"
	}

	return ctx.Status(fiber.StatusOK).JSON(response)
}

func (handler *Handler) publish(ctx context.Context, messageType string, payload any) error {
	payloadBytes, err := json.Marshal(payload)
	if err != nil {
		return fmt.Errorf("marshal queue payload: %w", err)
	}

	envelopeBytes, err := json.Marshal(QueueMessage{
		Type:    messageType,
		Payload: payloadBytes,
	})
	if err != nil {
		return fmt.Errorf("marshal queue message: %w", err)
	}

	return handler.publisher.Publish(ctx, ShipmentsQueue, envelopeBytes)
}

func validateShipmentRequest(request ShipmentRequest) error {
	if strings.TrimSpace(request.OriginID) == "" {
		return errors.New("origin_id is required")
	}

	if len(request.Items) == 0 {
		return errors.New("items are required")
	}

	for index, item := range request.Items {
		if item.Quantity <= 0 {
			return fmt.Errorf("items[%d].quantity must be greater than 0", index)
		}

		if item.ItemID == 0 && strings.TrimSpace(item.ItemName) == "" {
			return fmt.Errorf("items[%d] requires item_id or item_name", index)
		}
	}

	return nil
}

func validateDispatchRequest(request DispatchRequest) error {
	if strings.TrimSpace(request.ShipmentID) == "" {
		return errors.New("shipment_id is required")
	}

	return nil
}

func writeError(ctx *fiber.Ctx, status int, message string) error {
	return ctx.Status(status).JSON(fiber.Map{"error": message})
}
