package config

import (
	"fmt"
	"os"
	"strconv"
	"time"
)

const (
	defaultHTTPPort     = 8081
	defaultCoreTCPHost  = "127.0.0.1"
	defaultCoreTCPPort  = 10000
	defaultReadTimeout  = 5 * time.Second
	defaultWriteTimeout = 10 * time.Second
	defaultIdleTimeout  = 60 * time.Second
)

type Config struct {
	HTTPPort     int
	CoreTCPHost  string
	CoreTCPPort  int
	ReadTimeout  time.Duration
	WriteTimeout time.Duration
	IdleTimeout  time.Duration
}

func Load() Config {
	return Config{
		HTTPPort:     envInt("API_GATEWAY_PORT", defaultHTTPPort),
		CoreTCPHost:  envString("CORE_TCP_HOST", defaultCoreTCPHost),
		CoreTCPPort:  envInt("CORE_TCP_PORT", defaultCoreTCPPort),
		ReadTimeout:  envDuration("API_GATEWAY_READ_TIMEOUT", defaultReadTimeout),
		WriteTimeout: envDuration("API_GATEWAY_WRITE_TIMEOUT", defaultWriteTimeout),
		IdleTimeout:  envDuration("API_GATEWAY_IDLE_TIMEOUT", defaultIdleTimeout),
	}
}

func (cfg Config) Address() string {
	return fmt.Sprintf(":%d", cfg.HTTPPort)
}

func (cfg Config) CoreAddress() string {
	return fmt.Sprintf("%s:%d", cfg.CoreTCPHost, cfg.CoreTCPPort)
}

func envString(key, fallback string) string {
	value := os.Getenv(key)
	if value == "" {
		return fallback
	}

	return value
}

func envInt(key string, fallback int) int {
	value := os.Getenv(key)
	if value == "" {
		return fallback
	}

	parsed, err := strconv.Atoi(value)
	if err != nil {
		return fallback
	}

	return parsed
}

func envDuration(key string, fallback time.Duration) time.Duration {
	value := os.Getenv(key)
	if value == "" {
		return fallback
	}

	parsed, err := time.ParseDuration(value)
	if err != nil {
		return fallback
	}

	return parsed
}
