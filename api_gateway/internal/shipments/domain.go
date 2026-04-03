package shipments

type ShipmentRequest struct {
	OriginID      string         `json:"origin_id"`
	DestinationID string         `json:"destination_id"`
	Items         []ShipmentItem `json:"items"`
}

type ShipmentItem struct {
	ItemID   int    `json:"item_id"`
	ItemName string `json:"item_name"`
	Quantity int    `json:"quantity"`
}

type ShipmentResponse struct {
	ShipmentID string `json:"shipment_id"`
	Status     string `json:"status"`
}
