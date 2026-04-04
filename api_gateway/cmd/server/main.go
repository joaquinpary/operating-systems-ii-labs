package main

import (
	"context"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"

	"github.com/gofiber/fiber/v2"

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
	rabbitConnection, err := rabbitmq.Connect(cfg.RabbitMQURL)
	if err != nil {
		log.Fatalf("connect rabbitmq: %v", err)
	}
	shipmentHandler := shipments.NewHandler(corePool, rabbitConnection)
	dispatcherWorker := dispatcher.New(corePool)
	chatHub := chat.NewHub()
	predictorClient := predictor.NewClient(cfg.PredictorURL)

	jwtMiddleware := middleware.NewJWTMiddleware(cfg.JWTSecret)
	tracingMiddleware := middleware.NewTracingMiddleware()
	app := fiber.New(fiber.Config{AppName: "lora-chads-api-gateway"})
	app.Use(tracingMiddleware.Handler())

	protected := app.Group("", jwtMiddleware.Handler())
	protected.Post("/shipments", shipmentHandler.CreateShipment)
	protected.Post("/dispatch", shipmentHandler.Dispatch)
	protected.Get("/status/:id", shipmentHandler.GetStatus)

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

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

	components := []any{
		app,
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

	if err := app.Listen(fmt.Sprintf(":%d", cfg.HTTPPort)); err != nil {
		log.Printf("fiber server stopped: %v", err)
	}

	if err := dispatcherWorker.Stop(); err != nil {
		log.Printf("stop dispatcher worker: %v", err)
	}

	if err := rabbitConnection.Close(); err != nil {
		log.Printf("close rabbitmq connection: %v", err)
	}

	if err := corePool.Close(); err != nil {
		log.Printf("close core bridge pool: %v", err)
	}
}
