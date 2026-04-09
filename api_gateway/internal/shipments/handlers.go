package shipments

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"strings"

	"github.com/gofiber/fiber/v2"
	"github.com/golang-jwt/jwt/v5"

	"lora-chads/api_gateway/internal/core_bridge"
	"lora-chads/api_gateway/pkg/middleware"
)

type coreBridge interface {
	Query(ctx context.Context, shipmentID string) (core_bridge.Message, error)
	Command(ctx context.Context, command string, payload core_bridge.Payload) (core_bridge.Envelope, error)
}

type publisher interface {
	Publish(ctx context.Context, queue string, body []byte) error
}

type shipmentTracker interface {
	RegisterShipment(request ShipmentRequest) StatusResponse
	GetShipment(identifier string) (StatusResponse, bool)
	DeleteShipment(shipmentID string)
	CancelShipment(shipmentID string, callerUsername string) (StatusResponse, error)
	SnapshotForOwner(owner string) []StatusResponse
}

type Handler struct {
	bridge    coreBridge
	publisher publisher
	tracker   shipmentTracker
}

// NewHandler builds the HTTP shipment handler dependencies.
func NewHandler(bridge coreBridge, publisher publisher, tracker shipmentTracker) *Handler {
	return &Handler{
		bridge:    bridge,
		publisher: publisher,
		tracker:   tracker,
	}
}

// CreateShipment validates and enqueues a shipment creation request.
func (handler *Handler) CreateShipment(ctx *fiber.Ctx) error {
	var request ShipmentRequest
	if err := ctx.BodyParser(&request); err != nil {
		return writeError(ctx, fiber.StatusBadRequest, "invalid shipment request body")
	}
	request.Owner = callerUsername(ctx)
	requestID := middleware.RequestID(ctx)

	log.Printf("HTTP POST /shipments owner=%s items=%d request_id=%s", request.Owner, len(request.Items), requestID)

	if err := validateShipmentRequest(request); err != nil {
		return writeError(ctx, fiber.StatusBadRequest, err.Error())
	}

	status := handler.tracker.RegisterShipment(request)
	request.ShipmentID = status.ShipmentID

	if err := handler.publish(ctx.UserContext(), CreateShipmentMessageType, request); err != nil {
		handler.tracker.DeleteShipment(request.ShipmentID)
		return writeError(ctx, fiber.StatusInternalServerError, "failed to queue shipment request")
	}

	log.Printf("HTTP POST /shipments => 202 shipment_id=%s request_id=%s", request.ShipmentID, requestID)
	return ctx.Status(fiber.StatusAccepted).JSON(ShipmentResponse{
		ShipmentID: request.ShipmentID,
		Status:     StatusPendingConfirmation,
	})
}

// Dispatch validates and enqueues a shipment dispatch request.
func (handler *Handler) Dispatch(ctx *fiber.Ctx) error {
	var request DispatchRequest
	if err := ctx.BodyParser(&request); err != nil {
		return writeError(ctx, fiber.StatusBadRequest, "invalid dispatch request body")
	}
	owner := callerUsername(ctx)
	requestID := middleware.RequestID(ctx)

	log.Printf("HTTP POST /dispatch owner=%s shipment_id=%s request_id=%s", owner, request.ShipmentID, requestID)

	if err := validateDispatchRequest(request); err != nil {
		return writeError(ctx, fiber.StatusBadRequest, err.Error())
	}

	status, exists := handler.tracker.GetShipment(request.ShipmentID)
	if !exists {
		return writeError(ctx, fiber.StatusNotFound, "shipment not found")
	}
	if status.Owner != owner {
		return writeError(ctx, fiber.StatusForbidden, "shipment does not belong to current user")
	}

	if err := handler.publish(ctx.UserContext(), DispatchCommandType, request); err != nil {
		return writeError(ctx, fiber.StatusInternalServerError, "failed to queue dispatch command")
	}

	return ctx.Status(fiber.StatusAccepted).JSON(ShipmentResponse{
		ShipmentID: request.ShipmentID,
		Status:     StatusPendingConfirmation,
	})
}

// GetAllStatuses returns the tracked shipments owned by the current caller.
func (handler *Handler) GetAllStatuses(ctx *fiber.Ctx) error {
	return ctx.Status(fiber.StatusOK).JSON(handler.tracker.SnapshotForOwner(callerUsername(ctx)))
}

// GetStatus returns the current status for one tracked shipment.
func (handler *Handler) GetStatus(ctx *fiber.Ctx) error {
	identifier := strings.TrimSpace(ctx.Params("id"))
	if identifier == "" {
		return writeError(ctx, fiber.StatusBadRequest, "shipment id is required")
	}
	owner := callerUsername(ctx)
	requestID := middleware.RequestID(ctx)

	log.Printf("HTTP GET /status/%s owner=%s request_id=%s", identifier, owner, requestID)

	tracked, exists := handler.tracker.GetShipment(identifier)
	if !exists {
		return writeError(ctx, fiber.StatusNotFound, "shipment not found")
	}
	if tracked.Owner != owner {
		return writeError(ctx, fiber.StatusForbidden, "shipment does not belong to current user")
	}
	if tracked.TransactionID == "" {
		return ctx.Status(fiber.StatusOK).JSON(tracked)
	}

	response := tracked

	message, err := handler.bridge.Query(ctx.UserContext(), tracked.TransactionID)
	if err != nil {
		log.Printf("HTTP GET /status/%s: core query failed, using cached status %q: %v", identifier, tracked.Status, err)
	} else if len(message.Payload) > 0 {
		var corePayload struct {
			Status string `json:"status"`
		}
		if json.Unmarshal(message.Payload, &corePayload) == nil &&
			corePayload.Status != "" && corePayload.Status != "error" {
			response.Status = corePayload.Status
		}
	}

	return ctx.Status(fiber.StatusOK).JSON(response)
}

// publish wraps a queue payload in the shipments envelope and publishes it.
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

// validateShipmentRequest checks the HTTP shipment creation payload.
func validateShipmentRequest(request ShipmentRequest) error {
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

// validateDispatchRequest checks the HTTP dispatch payload.
func validateDispatchRequest(request DispatchRequest) error {
	if strings.TrimSpace(request.ShipmentID) == "" {
		return errors.New("shipment_id is required")
	}

	return nil
}

// writeError sends a JSON error response with the provided status code.
func writeError(ctx *fiber.Ctx, status int, message string) error {
	return ctx.Status(status).JSON(fiber.Map{"error": message})
}

// callerUsername extracts the authenticated username from JWT claims.
func callerUsername(ctx *fiber.Ctx) string {
	claims, ok := ctx.Locals("user").(jwt.MapClaims)
	if !ok {
		return "gateway-internal"
	}

	for _, key := range []string{"sub", "username", "user_id"} {
		value, ok := claims[key]
		if !ok {
			continue
		}

		owner := strings.TrimSpace(fmt.Sprint(value))
		if owner != "" {
			return owner
		}
	}

	return "gateway-internal"
}
