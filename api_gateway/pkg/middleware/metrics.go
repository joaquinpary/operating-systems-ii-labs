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

// NewMetricsMiddleware registers and returns the Prometheus HTTP middleware.
func NewMetricsMiddleware() MetricsMiddleware {
	registerMetricsOnce.Do(func() {
		prometheus.MustRegister(httpRequestsTotal, httpRequestDuration, httpRequestsInFlight)
	})

	return MetricsMiddleware{}
}

// Handler records request metrics for non-metrics and non-health endpoints.
func (middleware MetricsMiddleware) Handler() fiber.Handler {
	return func(ctx *fiber.Ctx) error {
		p := ctx.Path()
		if p == "/metrics" || p == "/health" {
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

		method := sanitizeMethod(ctx.Method())
		path := routePattern(ctx)
		httpRequestsTotal.WithLabelValues(method, path, strconv.Itoa(statusCode)).Inc()
		httpRequestDuration.WithLabelValues(method, path).Observe(time.Since(startedAt).Seconds())

		return err
	}
}

// sanitizeMethod restricts recorded HTTP methods to a known set, preventing
// unbounded label cardinality and Prometheus collection conflicts from
// malformed requests.
func sanitizeMethod(m string) string {
	switch strings.ToUpper(m) {
	case "GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS":
		return strings.ToUpper(m)
	default:
		return "OTHER"
	}
}

// routePattern returns the matched Fiber route path for metrics labels.
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
