package main

import (
"context"
"fmt"
"log"
"os/signal"
"syscall"

"github.com/gofiber/fiber/v2"

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

	// --- RabbitMQ ---
	rabbitConnection, err := rabbitmq.Connect(cfg.RabbitMQURL)
	if err != nil {
		log.Fatalf("connect rabbitmq: %v", err)
	}
	defer rabbitConnection.Close()

	// --- Application components ---
	shipmentHandler := shipments.NewHandler(pool, rabbitConnection)
	dispatcherWorker := dispatcher.New(pool)
	chatHub := chat.NewHub()
	predictorClient := predictor.NewClient(cfg.PredictorURL)

	// --- JWT & tracing middleware ---
	jwtMiddleware := middleware.NewJWTMiddleware(cfg.JWTSecret)
	tracingMiddleware := middleware.NewTracingMiddleware()

	// --- Fiber HTTP server ---
	app := fiber.New(fiber.Config{AppName: "lora-chads-api-gateway"})
	app.Use(tracingMiddleware.Handler())

	protected := app.Group("", jwtMiddleware.Handler())
	protected.Post("/shipments", shipmentHandler.CreateShipment)
	protected.Post("/dispatch", shipmentHandler.Dispatch)
	protected.Get("/status/:id", shipmentHandler.GetStatus)

	// --- Background workers ---
	go func() {
		<-ctx.Done()
		log.Printf("shutdown signal received")
		if err := app.Shutdown(); err != nil {
			log.Printf("shutdown fiber app: %v", err)
		}
	}()

	go func() {
		if err := dispatcherWorker.Start(ctx); err != nil {
			log.Printf("dispatcher stopped: %v", err)
		}
	}()

	go func() {
		if err := chatHub.Run(ctx); err != nil {
			log.Printf("chat hub stopped: %v", err)
		}
	}()

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

	if err := app.Listen(fmt.Sprintf(":%d", cfg.HTTPPort)); err != nil {
		log.Printf("fiber server stopped: %v", err)
	}

	if err := dispatcherWorker.Stop(); err != nil {
		log.Printf("stop dispatcher worker: %v", err)
	}
}
