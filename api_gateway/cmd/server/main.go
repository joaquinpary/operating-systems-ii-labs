package main

import (
	"log"

	"lora-chads/api_gateway/internal/chat"
	"lora-chads/api_gateway/internal/config"
	"lora-chads/api_gateway/internal/core_bridge"
	"lora-chads/api_gateway/internal/dispatcher"
	"lora-chads/api_gateway/internal/predictor"
	"lora-chads/api_gateway/internal/shipments"
	"lora-chads/api_gateway/pkg/middleware"
	"lora-chads/api_gateway/pkg/rabbitmq"
)

func main() {
	cfg, err := config.Load()
	if err != nil {
		log.Fatalf("load config: %v", err)
	}

	corePool := core_bridge.NewPool(cfg.CoreHost, cfg.CorePort)
	shipmentHandler := shipments.NewHandler(corePool)
	dispatcherWorker := dispatcher.New(corePool)
	chatHub := chat.NewHub()
	predictorClient := predictor.NewClient(cfg.PredictorURL)
	rabbitConnection, err := rabbitmq.Connect(cfg.RabbitMQURL)
	if err != nil {
		log.Fatalf("connect rabbitmq: %v", err)
	}

	jwtMiddleware := middleware.NewJWTMiddleware(cfg.JWTSecret)
	tracingMiddleware := middleware.NewTracingMiddleware()

	components := []any{
		shipmentHandler,
		dispatcherWorker,
		chatHub,
		predictorClient,
		rabbitConnection,
		jwtMiddleware,
		tracingMiddleware,
	}

	log.Printf(
		"api gateway skeleton initialized: core=%s http_port=%d rabbitmq=%s predictor=%s components=%d jwt=%t tracing=%s",
		corePool.Address(),
		cfg.HTTPPort,
		rabbitConnection.URL(),
		predictorClient.BaseURL(),
		len(components),
		jwtMiddleware.Enabled(),
		tracingMiddleware.Name(),
	)

	// TODO: start the HTTP server and register shipment routes.
	// TODO: start the dispatcher worker and core bridge listener.
	// TODO: add graceful shutdown for all long-lived components.

	if err := rabbitConnection.Close(); err != nil {
		log.Printf("close rabbitmq connection: %v", err)
	}

	if err := corePool.Close(); err != nil {
		log.Printf("close core bridge pool: %v", err)
	}
}
