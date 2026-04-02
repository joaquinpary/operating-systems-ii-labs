package shipments

import "time"

type Status string

const (
	StatusPending    Status = "pending"
	StatusAssigned   Status = "assigned"
	StatusDispatched Status = "dispatched"
	StatusCompleted  Status = "completed"
)

type Item struct {
	Name     string `json:"name"`
	Quantity int    `json:"quantity"`
}

type Shipment struct {
	ID          string    `json:"id"`
	Source      string    `json:"source"`
	Destination string    `json:"destination"`
	Items       []Item    `json:"items"`
	Status      Status    `json:"status"`
	CreatedAt   time.Time `json:"created_at"`
}
