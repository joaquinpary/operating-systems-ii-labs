package middleware

import (
	"crypto/rand"
	"encoding/hex"

	"github.com/gofiber/fiber/v2"
)

const requestIDHeader = "X-Request-ID"

type TracingMiddleware struct{}

func NewTracingMiddleware() TracingMiddleware {
	return TracingMiddleware{}
}

func (middleware TracingMiddleware) Name() string {
	return "request-id"
}

func (middleware TracingMiddleware) Handler() fiber.Handler {
	return func(ctx *fiber.Ctx) error {
		requestID := ctx.Get(requestIDHeader)
		if requestID == "" {
			requestID = newRequestID()
		}

		ctx.Locals("request_id", requestID)
		ctx.Set(requestIDHeader, requestID)

		return ctx.Next()
	}
}

func newRequestID() string {
	buffer := make([]byte, 16)
	if _, err := rand.Read(buffer); err != nil {
		return "request-id-unavailable"
	}

	return hex.EncodeToString(buffer)
}
