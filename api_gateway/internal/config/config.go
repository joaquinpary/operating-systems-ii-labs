package config

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"time"
)

const (
	defaultCoreHost         = "127.0.0.1"
	defaultCorePort         = 9999
	defaultHTTPPort         = 8081
	defaultRabbitMQURL      = "amqp://guest:guest@localhost:5672/"
	defaultJWTSecret        = "change-me"
	defaultPredictorURL     = "http://127.0.0.1:9000"
	defaultCredentialsDir   = "../config/gateway"
	defaultCorePoolSize     = 5
	defaultCoreConnTimeout  = 10 * time.Second
	defaultCoreKeepaliveIvl = 60 * time.Second
	defaultCoreSourceID     = "api_gateway"
	defaultCorePasswordMD5  = "667b71dd38a514eecb68eaf05afb9402"
	defaultEurekaURL        = "http://localhost:8761/eureka"
	defaultEurekaAppName    = "API-GATEWAY"
)

type Config struct {
	CoreHost         string
	CorePort         int
	HTTPPort         int
	RabbitMQURL      string
	JWTSecret        string
	PredictorURL     string
	CredentialsDir   string
	CorePoolSize     int
	CoreConnTimeout  time.Duration
	CoreKeepaliveIvl time.Duration
	CoreSourceID     string
	CorePasswordMD5  string
	EurekaURL        string
	EurekaAppName    string
	EurekaEnabled    bool
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

	credentialsDir := resolveDirPath(stringFromEnv("CREDENTIALS_DIR", defaultCredentialsDir))

	eurekaEnabled := stringFromEnv("EUREKA_ENABLED", "true")

	return Config{
		CoreHost:         stringFromEnv("CORE_HOST", defaultCoreHost),
		CorePort:         corePort,
		HTTPPort:         httpPort,
		RabbitMQURL:      stringFromEnv("RABBITMQ_URL", defaultRabbitMQURL),
		JWTSecret:        stringFromEnv("JWT_SECRET", defaultJWTSecret),
		PredictorURL:     stringFromEnv("PREDICTOR_URL", defaultPredictorURL),
		CredentialsDir:   credentialsDir,
		CorePoolSize:     defaultCorePoolSize,
		CoreConnTimeout:  defaultCoreConnTimeout,
		CoreKeepaliveIvl: defaultCoreKeepaliveIvl,
		CoreSourceID:     defaultCoreSourceID,
		CorePasswordMD5:  defaultCorePasswordMD5,
		EurekaURL:        stringFromEnv("EUREKA_URL", defaultEurekaURL),
		EurekaAppName:    stringFromEnv("EUREKA_APP_NAME", defaultEurekaAppName),
		EurekaEnabled:    eurekaEnabled == "true" || eurekaEnabled == "1",
	}, nil
}

func (cfg Config) CoreAddress() string {
	return fmt.Sprintf("%s:%d", cfg.CoreHost, cfg.CorePort)
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

func resolveDirPath(value string) string {
	if value == "" || filepath.IsAbs(value) {
		return value
	}

	workingDir, err := os.Getwd()
	if err != nil {
		return value
	}

	dir := workingDir
	for {
		candidate := filepath.Join(dir, value)
		info, err := os.Stat(candidate)
		if err == nil && info.IsDir() {
			return candidate
		}

		parent := filepath.Dir(dir)
		if parent == dir {
			return value
		}

		dir = parent
	}
}
