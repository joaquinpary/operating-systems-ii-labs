package main

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"

	ws "github.com/gofiber/contrib/websocket"
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

const emergencyAlertMsgType corebridge.MsgType = "SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT"

func main() {
	// --- Log to both stdout and file ---
	logPath := filepath.Join("logs", "server", "server_go_gateway.log")
	if err := os.MkdirAll(filepath.Dir(logPath), 0o755); err != nil {
		log.Fatalf("create log dir: %v", err)
	}
	logFile, err := os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o644)
	if err != nil {
		log.Fatalf("open log file: %v", err)
	}
	defer logFile.Close()
	log.SetOutput(io.MultiWriter(os.Stdout, logFile))
	log.SetFlags(log.Ldate | log.Ltime | log.Lmicroseconds)

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

	// --- RabbitMQ ---
	rabbitConnection, err := rabbitmq.Connect(cfg.RabbitMQURL)
	if err != nil {
		log.Fatalf("connect rabbitmq: %v", err)
	}
	defer rabbitConnection.Close()

	// --- Application components ---
	dispatcherWorker := dispatcher.New(pool, rabbitConnection, cfg.CorePoolSize)
	shipmentHandler := shipments.NewHandler(pool, rabbitConnection, dispatcherWorker)
	chatHub := chat.NewHub(cfg.JWTSecret, log.Default(), dispatcherWorker)
	predictorClient := predictor.NewClient(cfg.PredictorURL)

	// --- Event listener (authenticates LAST → reverse-map in C++) ---
	listener := corebridge.NewListener(corebridge.ListenerConfig{
		Addr:         cfg.CoreAddress(),
		SourceID:     cfg.CoreSourceID,
		PasswordMD5:  cfg.CorePasswordMD5,
		ConnTimeout:  cfg.CoreConnTimeout,
		KeepaliveIvl: cfg.CoreKeepaliveIvl,
	})
	listener.OnEvent(func(env corebridge.Envelope) {
		payload, err := json.Marshal(env.Payload)
		if err != nil {
			log.Printf("listener: marshal event payload %s: %v", env.MsgType, err)
		} else {
			log.Printf("listener: event %s from %s payload=%s", env.MsgType, env.SourceID, string(payload))
		}
		if env.MsgType == emergencyAlertMsgType {
			chatHub.BroadcastCoreEvent(env)
		}
	})

	if err := listener.Start(ctx); err != nil {
		log.Fatalf("core_bridge listener: %v", err)
	}
	defer listener.Close()

	// --- JWT & tracing middleware ---
	jwtMiddleware := middleware.NewJWTMiddleware(cfg.JWTSecret)
	tracingMiddleware := middleware.NewTracingMiddleware()

	// --- Fiber HTTP server ---
	app := fiber.New(fiber.Config{AppName: "lora-chads-api-gateway"})
	app.Use(tracingMiddleware.Handler())
	app.Get("/ws/chat", chatHub.HandleUpgrade, ws.New(chatHub.HandleWS))

	protected := app.Group("", jwtMiddleware.Handler())
	protected.Post("/shipments", shipmentHandler.CreateShipment)
	protected.Post("/dispatch", shipmentHandler.Dispatch)
	protected.Get("/status", shipmentHandler.GetAllStatuses)
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

	stop()

	if err := dispatcherWorker.Stop(); err != nil {
		log.Printf("stop dispatcher worker: %v", err)
	}
}
