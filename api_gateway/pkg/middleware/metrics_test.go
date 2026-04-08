package middleware

import (
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/gofiber/fiber/v2"
	"github.com/prometheus/client_golang/prometheus/testutil"
)

func TestMetricsMiddlewareRecordsRoutePattern(t *testing.T) {
	app := fiber.New()
	app.Use(NewMetricsMiddleware().Handler())
	app.Get("/status/:id", func(ctx *fiber.Ctx) error {
		return ctx.SendStatus(fiber.StatusOK)
	})

	before := testutil.ToFloat64(httpRequestsTotal.WithLabelValues(http.MethodGet, "/status/:id", "200"))

	req := httptest.NewRequest(http.MethodGet, "/status/abc-123", nil)
	resp, err := app.Test(req)
	if err != nil {
		t.Fatalf("app.Test: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != fiber.StatusOK {
		t.Fatalf("expected 200 OK, got %d", resp.StatusCode)
	}

	after := testutil.ToFloat64(httpRequestsTotal.WithLabelValues(http.MethodGet, "/status/:id", "200"))
	if after != before+1 {
		t.Fatalf("expected counter to increment by 1, before=%v after=%v", before, after)
	}

	inFlight := testutil.ToFloat64(httpRequestsInFlight)
	if inFlight != 0 {
		t.Fatalf("expected no in-flight requests after completion, got %v", inFlight)
	}
}
