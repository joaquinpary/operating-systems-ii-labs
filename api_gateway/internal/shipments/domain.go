package shipments

import "encoding/json"

const (
	ShipmentsQueue            = "shipments"
	CreateShipmentMessageType = "create_shipment"
	DispatchCommandType       = "dispatch_command"

	StatusPendingConfirmation = "pending_confirmation"
	StatusConfirmed           = "confirm"
	StatusCancelled           = "cancelled"
)

type ShipmentRequest struct {
	ShipmentID string         `json:"shipment_id,omitempty"`
	Owner      string         `json:"owner,omitempty"`
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

type CreateShipmentData struct {
	DispatchHubID string `json:"dispatch_hub_id"`
	TransactionID int    `json:"transaction_id"`
}

type CreateShipmentResponse struct {
	Status        string `json:"status"`
	DispatchHubID string `json:"dispatch_hub_id,omitempty"`
	TransactionID int    `json:"transaction_id,omitempty"`
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
	Owner         string         `json:"owner,omitempty"`
	Status        string         `json:"status"`
	CreatedAt     string         `json:"created_at,omitempty"`
	Items         []ShipmentItem `json:"items,omitempty"`
}
