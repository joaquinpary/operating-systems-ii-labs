package shipments

import "encoding/json"

const (
	ShipmentsQueue            = "shipments"
	CreateShipmentMessageType = "create_shipment"
	DispatchCommandType       = "dispatch_command"

	StatusPendingConfirmation = "pending_confirmation"
	StatusConfirmed           = "confirm"
)

type ShipmentRequest struct {
	ShipmentID string         `json:"shipment_id,omitempty"`
	OriginID   string         `json:"origin_id"`
	Items      []ShipmentItem `json:"items"`
}

type ShipmentItem struct {
	ItemID   int    `json:"item_id"`
	ItemName string `json:"item_name"`
	Quantity int    `json:"quantity"`
}

type ShipmentResponse struct {
	ShipmentID string `json:"shipment_id,omitempty"`
	Status     string `json:"status"`
}

type DispatchRequest struct {
	ShipmentID string `json:"shipment_id"`
}

type QueueMessage struct {
	Type    string          `json:"type"`
	Payload json.RawMessage `json:"payload"`
}

type StatusResponse struct {
	ShipmentID    string         `json:"shipment_id,omitempty"`
	TransactionID string         `json:"transaction_id,omitempty"`
	OriginID      string         `json:"origin_id,omitempty"`
	Status        string         `json:"status"`
	CreatedAt     string         `json:"created_at,omitempty"`
	Items         []ShipmentItem `json:"items,omitempty"`
}
