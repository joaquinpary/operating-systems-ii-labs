package shipments

import "encoding/json"

type ShipmentRequest struct {
	OriginID string         `json:"origin_id"`
	Items    []ShipmentItem `json:"items"`
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
	ShipmentID string         `json:"shipment_id"`
	Status     string         `json:"status"`
	Items      []ShipmentItem `json:"items,omitempty"`
}
