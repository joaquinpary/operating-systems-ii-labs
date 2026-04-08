package middleware

import (
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/gofiber/fiber/v2"
	"github.com/prometheus/client_golang/prometheus"
)

var (
	registerMetricsOnce sync.Once

	httpRequestsTotal = prometheus.NewCounterVec(
		prometheus.CounterOpts{
			Namespace: "api_gateway",
			Name:      "http_requests_total",
			Help:      "Total number of HTTP requests handled by the API gateway.",
		},
		[]string{"method", "path", "status"},
	)
	httpRequestDuration = prometheus.NewHistogramVec(
		prometheus.HistogramOpts{
			Namespace: "api_gateway",
			Name:      "http_request_duration_seconds",
			Help:      "Duration of HTTP requests handled by the API gateway.",
			Buckets:   prometheus.DefBuckets,
		},
		[]string{"method", "path"},
	)
	httpRequestsInFlight = prometheus.NewGauge(
		prometheus.GaugeOpts{
			Namespace: "api_gateway",
			Name:      "http_requests_in_flight",
			Help:      "Current number of in-flight HTTP requests being handled by the API gateway.",
		},
	)
)

type MetricsMiddleware struct{}

func NewMetricsMiddleware() MetricsMiddleware {
	registerMetricsOnce.Do(func() {
		prometheus.MustRegister(httpRequestsTotal, httpRequestDuration, httpRequestsInFlight)
	})

	return MetricsMiddleware{}
}

func (middleware MetricsMiddleware) Handler() fiber.Handler {
	return func(ctx *fiber.Ctx) error {
		if ctx.Path() == "/metrics" {
			return ctx.Next()
		}

		httpRequestsInFlight.Inc()
		startedAt := time.Now()

		err := ctx.Next()

		httpRequestsInFlight.Dec()

		statusCode := ctx.Response().StatusCode()
		if err != nil && statusCode < fiber.StatusBadRequest {
			statusCode = fiber.StatusInternalServerError
		}

		path := routePattern(ctx)
		httpRequestsTotal.WithLabelValues(ctx.Method(), path, strconv.Itoa(statusCode)).Inc()
		httpRequestDuration.WithLabelValues(ctx.Method(), path).Observe(time.Since(startedAt).Seconds())

		return err
	}
}

func routePattern(ctx *fiber.Ctx) string {
	if ctx == nil {
		return "unmatched"
	}

	route := ctx.Route()
	if route == nil {
		return "unmatched"
	}

	path := strings.TrimSpace(route.Path)
	if path == "" {
		return "unmatched"
	}

	return path
}
