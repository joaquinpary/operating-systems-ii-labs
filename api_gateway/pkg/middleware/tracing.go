package middleware

import (
	"crypto/rand"
	"encoding/hex"
	"log"
	"strings"
	"time"

	"github.com/gofiber/fiber/v2"
)

const requestIDHeader = "X-Request-ID"
const requestIDLocalKey = "request_id"

type TracingMiddleware struct{}

// NewTracingMiddleware builds the request ID middleware.
func NewTracingMiddleware() TracingMiddleware {
	return TracingMiddleware{}
}

// Name returns the tracing middleware identifier.
func (middleware TracingMiddleware) Name() string {
	return "request-id"
}

// Handler injects a request ID and logs each HTTP request.
func (middleware TracingMiddleware) Handler() fiber.Handler {
	return func(ctx *fiber.Ctx) error {
		startedAt := time.Now()
		requestID := requestIDFromContext(ctx)
		if requestID == "" {
			requestID = newRequestID()
		}

		ctx.Locals(requestIDLocalKey, requestID)
		ctx.Set(requestIDHeader, requestID)

		err := ctx.Next()
		statusCode := ctx.Response().StatusCode()
		if err != nil && statusCode < fiber.StatusBadRequest {
			statusCode = fiber.StatusInternalServerError
		}

		log.Printf(
			"HTTP %s %s => %d duration=%s request_id=%s",
			ctx.Method(),
			ctx.OriginalURL(),
			statusCode,
			time.Since(startedAt).Round(time.Microsecond),
			requestID,
		)

		return err
	}
}

// RequestID returns the current request identifier for the Fiber context.
func RequestID(ctx *fiber.Ctx) string {
	requestID := requestIDFromContext(ctx)
	if requestID == "" {
		return "request-id-unavailable"
	}

	return requestID
}

// requestIDFromContext extracts the request ID from locals or headers.
func requestIDFromContext(ctx *fiber.Ctx) string {
	if ctx == nil {
		return ""
	}

	if requestID, ok := ctx.Locals(requestIDLocalKey).(string); ok {
		requestID = strings.TrimSpace(requestID)
		if requestID != "" {
			return requestID
		}
	}

	return strings.TrimSpace(ctx.Get(requestIDHeader))
}

// newRequestID generates a random request identifier.
func newRequestID() string {
	buffer := make([]byte, 16)
	if _, err := rand.Read(buffer); err != nil {
		return "request-id-unavailable"
	}

	return hex.EncodeToString(buffer)
}
