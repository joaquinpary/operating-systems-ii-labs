package main

import (
	"context"
	"log"
	"os/signal"
	"syscall"

	"lora-chads/api-gateway/internal/chat"
	"lora-chads/api-gateway/internal/platform/config"
	platformserver "lora-chads/api-gateway/internal/platform/server"
	"lora-chads/api-gateway/internal/predictor"
	"lora-chads/api-gateway/internal/shipments"
)

func main() {
	cfg := config.Load()

	srv := platformserver.New(
		cfg,
		shipments.NewHandler(nil),
		chat.NewHandler(nil),
		predictor.NewHandler(nil),
	)

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	if err := srv.Run(ctx); err != nil {
		log.Fatal(err)
	}
}
