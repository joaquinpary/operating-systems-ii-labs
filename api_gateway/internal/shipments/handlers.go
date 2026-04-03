package shipments

import (
	"context"

	"lora-chads/api_gateway/internal/core_bridge"
)

type Handler struct {
	pool *core_bridge.Pool
}

func NewHandler(pool *core_bridge.Pool) *Handler {
	return &Handler{pool: pool}
}

func (handler *Handler) CreateShipment(_ context.Context, _ ShipmentRequest) (ShipmentResponse, error) {
	return ShipmentResponse{}, nil
}

func (handler *Handler) GetStatus(_ context.Context, shipmentID string) (ShipmentResponse, error) {
	return ShipmentResponse{
		ShipmentID: shipmentID,
	}, nil
}
