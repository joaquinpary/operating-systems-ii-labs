package core_bridge

import (
	"context"
	"encoding/json"
	"fmt"
)

type Pool struct {
	host string
	port int
}

func NewPool(host string, port int) *Pool {
	return &Pool{
		host: host,
		port: port,
	}
}

func (pool *Pool) Address() string {
	return fmt.Sprintf("%s:%d", pool.host, pool.port)
}

func (pool *Pool) Send(_ Message) error {
	return nil
}

func (pool *Pool) Query(_ context.Context, shipmentID string) (Message, error) {
	payload, err := json.Marshal(map[string]string{
		"shipment_id": shipmentID,
		"status":      "pending",
	})
	if err != nil {
		return Message{}, fmt.Errorf("marshal shipment status: %w", err)
	}

	return Message{
		SourceRole: RoleServer,
		TargetID:   shipmentID,
		Payload:    payload,
	}, nil
}

func (pool *Pool) Close() error {
	return nil
}
