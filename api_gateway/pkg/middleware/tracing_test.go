package middleware

import (
	"io"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/gofiber/fiber/v2"
)

func TestTracingMiddlewareGeneratesRequestID(t *testing.T) {
	app := fiber.New()
	app.Use(NewTracingMiddleware().Handler())
	app.Get("/trace", func(ctx *fiber.Ctx) error {
		return ctx.SendString(RequestID(ctx))
	})

	req := httptest.NewRequest(http.MethodGet, "/trace", nil)
	resp, err := app.Test(req)
	if err != nil {
		t.Fatalf("app.Test: %v", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatalf("io.ReadAll: %v", err)
	}

	requestID := resp.Header.Get(requestIDHeader)
	if requestID == "" {
		t.Fatalf("expected %s response header to be set", requestIDHeader)
	}

	if string(body) != requestID {
		t.Fatalf("expected body to echo request ID %q, got %q", requestID, string(body))
	}
}

func TestTracingMiddlewarePreservesIncomingRequestID(t *testing.T) {
	app := fiber.New()
	app.Use(NewTracingMiddleware().Handler())
	app.Get("/trace", func(ctx *fiber.Ctx) error {
		return ctx.SendString(RequestID(ctx))
	})

	req := httptest.NewRequest(http.MethodGet, "/trace", nil)
	req.Header.Set(requestIDHeader, "external-request-id")

	resp, err := app.Test(req)
	if err != nil {
		t.Fatalf("app.Test: %v", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		t.Fatalf("io.ReadAll: %v", err)
	}

	if got := resp.Header.Get(requestIDHeader); got != "external-request-id" {
		t.Fatalf("expected response header to preserve request ID, got %q", got)
	}

	if string(body) != "external-request-id" {
		t.Fatalf("expected body to echo preserved request ID, got %q", string(body))
	}
}
