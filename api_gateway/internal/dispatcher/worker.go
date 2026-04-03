package dispatcher

import (
	"context"

	"lora-chads/api_gateway/internal/core_bridge"
)

type Dispatcher struct {
	pool *core_bridge.Pool
}

func New(pool *core_bridge.Pool) *Dispatcher {
	return &Dispatcher{pool: pool}
}

func (dispatcher *Dispatcher) Start(_ context.Context) error {
	return nil
}

func (dispatcher *Dispatcher) Stop() error {
	return nil
}
