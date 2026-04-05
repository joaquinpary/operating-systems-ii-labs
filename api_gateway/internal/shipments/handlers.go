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

const (
	shipmentsQueue            = "shipments"
	createShipmentMessageType = "create_shipment"
	dispatchCommandType       = "dispatch_command"
)

type statusQuerier interface {
	Query(ctx context.Context, shipmentID string) (core_bridge.Message, error)
}

type publisher interface {
	Publish(ctx context.Context, queue string, body []byte) error
}

type Handler struct {
	pool      statusQuerier
	publisher publisher
}

func NewHandler(pool statusQuerier, publisher publisher) *Handler {
	return &Handler{
		pool:      pool,
		publisher: publisher,
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

	if err := handler.publish(ctx.UserContext(), createShipmentMessageType, request); err != nil {
		return writeError(ctx, fiber.StatusInternalServerError, "failed to queue shipment request")
	}

	return ctx.Status(fiber.StatusAccepted).JSON(ShipmentResponse{Status: "accepted"})
}

func (handler *Handler) Dispatch(ctx *fiber.Ctx) error {
	var request DispatchRequest
	if err := ctx.BodyParser(&request); err != nil {
		return writeError(ctx, fiber.StatusBadRequest, "invalid dispatch request body")
	}

	if err := validateDispatchRequest(request); err != nil {
		return writeError(ctx, fiber.StatusBadRequest, err.Error())
	}

	if err := handler.publish(ctx.UserContext(), dispatchCommandType, request); err != nil {
		return writeError(ctx, fiber.StatusInternalServerError, "failed to queue dispatch command")
	}

	return ctx.Status(fiber.StatusAccepted).JSON(ShipmentResponse{
		ShipmentID: request.ShipmentID,
		Status:     "accepted",
	})
}

func (handler *Handler) GetStatus(ctx *fiber.Ctx) error {
	shipmentID := strings.TrimSpace(ctx.Params("id"))
	if shipmentID == "" {
		return writeError(ctx, fiber.StatusBadRequest, "shipment id is required")
	}

	message, err := handler.pool.Query(ctx.UserContext(), shipmentID)
	if err != nil {
		return writeError(ctx, fiber.StatusInternalServerError, "failed to query shipment status")
	}

	response := StatusResponse{
		ShipmentID: shipmentID,
		Status:     "unknown",
	}

	if len(message.Payload) > 0 {
		if err := json.Unmarshal(message.Payload, &response); err != nil {
			return writeError(ctx, fiber.StatusInternalServerError, "failed to decode status response")
		}
	}

	if response.ShipmentID == "" {
		response.ShipmentID = shipmentID
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

	return handler.publisher.Publish(ctx, shipmentsQueue, envelopeBytes)
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
