package chat

import "context"

type Hub struct{}

func NewHub() *Hub {
	return &Hub{}
}

func (hub *Hub) Run(_ context.Context) error {
	return nil
}
