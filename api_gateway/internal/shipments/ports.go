package shipments

import "context"

type Service interface {
	CreateShipment(ctx context.Context, shipment Shipment) (Shipment, error)
	GetShipment(ctx context.Context, shipmentID string) (Shipment, error)
}

type Transport interface {
	DispatchShipment(ctx context.Context, shipment Shipment) error
	FetchShipment(ctx context.Context, shipmentID string) (Shipment, error)
}
