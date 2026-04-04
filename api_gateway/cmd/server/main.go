package main

import (
	"context"
	"log"
	"os/signal"
	"syscall"

	"lora-chads/api_gateway/internal/chat"
	"lora-chads/api_gateway/internal/config"
	corebridge "lora-chads/api_gateway/internal/core_bridge"
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

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	// --- TCP connection pool (N authenticated connections) ---
	pool := corebridge.NewPool(corebridge.PoolConfig{
		Addr:         cfg.CoreAddress(),
		SourceID:     cfg.CoreSourceID,
		PasswordMD5:  cfg.CorePasswordMD5,
		Size:         cfg.CorePoolSize,
		ConnTimeout:  cfg.CoreConnTimeout,
		KeepaliveIvl: cfg.CoreKeepaliveIvl,
	})

	if err := pool.Open(ctx); err != nil {
		log.Fatalf("core_bridge pool: %v", err)
	}
	defer pool.Close()

	// --- Event listener (authenticates LAST → reverse-map in C++) ---
	listener := corebridge.NewListener(corebridge.ListenerConfig{
		Addr:         cfg.CoreAddress(),
		SourceID:     cfg.CoreSourceID,
		PasswordMD5:  cfg.CorePasswordMD5,
		ConnTimeout:  cfg.CoreConnTimeout,
		KeepaliveIvl: cfg.CoreKeepaliveIvl,
	})
	listener.OnEvent(func(env corebridge.Envelope) {
		log.Printf("listener: event %s from %s", env.MsgType, env.SourceID)
	})

	if err := listener.Start(ctx); err != nil {
		log.Fatalf("core_bridge listener: %v", err)
	}
	defer listener.Close()

	// --- Companion components (stubs wired by teammate in #121) ---
	shipmentHandler := shipments.NewHandler(pool)
	dispatcherWorker := dispatcher.New(pool)
	chatHub := chat.NewHub()
	predictorClient := predictor.NewClient(cfg.PredictorURL)
	rabbitConnection, err := rabbitmq.Connect(cfg.RabbitMQURL)
	if err != nil {
		log.Fatalf("connect rabbitmq: %v", err)
	}
	defer rabbitConnection.Close()

	jwtMiddleware := middleware.NewJWTMiddleware(cfg.JWTSecret)
	tracingMiddleware := middleware.NewTracingMiddleware()

	log.Printf(
		"api gateway ready: core=%s pool_size=%d http_port=%d rabbitmq=%s predictor=%s jwt=%t tracing=%s",
		cfg.CoreAddress(),
		cfg.CorePoolSize,
		cfg.HTTPPort,
		rabbitConnection.URL(),
		predictorClient.BaseURL(),
		jwtMiddleware.Enabled(),
		tracingMiddleware.Name(),
	)

	_ = shipmentHandler
	_ = dispatcherWorker
	_ = chatHub

	// TODO: start the HTTP server (Fiber) and register shipment routes.
	// TODO: start the dispatcher worker consumer loop.

	<-ctx.Done()
	log.Println("shutdown signal received")
}
