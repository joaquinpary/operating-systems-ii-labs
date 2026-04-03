package config

import (
	"fmt"
	"os"
	"strconv"
)

const (
	defaultCoreHost     = "127.0.0.1"
	defaultCorePort     = 9999
	defaultHTTPPort     = 8080
	defaultRabbitMQURL  = "amqp://guest:guest@localhost:5672/"
	defaultJWTSecret    = "change-me"
	defaultPredictorURL = "http://127.0.0.1:9000"
)

type Config struct {
	CoreHost     string
	CorePort     int
	HTTPPort     int
	RabbitMQURL  string
	JWTSecret    string
	PredictorURL string
}

func Load() (Config, error) {
	corePort, err := intFromEnv("CORE_PORT", defaultCorePort)
	if err != nil {
		return Config{}, err
	}

	httpPort, err := intFromEnv("HTTP_PORT", defaultHTTPPort)
	if err != nil {
		return Config{}, err
	}

	return Config{
		CoreHost:     stringFromEnv("CORE_HOST", defaultCoreHost),
		CorePort:     corePort,
		HTTPPort:     httpPort,
		RabbitMQURL:  stringFromEnv("RABBITMQ_URL", defaultRabbitMQURL),
		JWTSecret:    stringFromEnv("JWT_SECRET", defaultJWTSecret),
		PredictorURL: stringFromEnv("PREDICTOR_URL", defaultPredictorURL),
	}, nil
}

func stringFromEnv(key, fallback string) string {
	value := os.Getenv(key)
	if value == "" {
		return fallback
	}

	return value
}

func intFromEnv(key string, fallback int) (int, error) {
	value := os.Getenv(key)
	if value == "" {
		return fallback, nil
	}

	parsed, err := strconv.Atoi(value)
	if err != nil {
		return 0, fmt.Errorf("parse %s: %w", key, err)
	}

	return parsed, nil
}
