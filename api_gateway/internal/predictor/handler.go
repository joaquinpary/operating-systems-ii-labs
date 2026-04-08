package predictor

import (
	"log"

	"github.com/gofiber/fiber/v2"

	"lora-chads/api_gateway/pkg/middleware"
)

type Handler struct {
	client *Client
}

func NewHandler(client *Client) *Handler {
	return &Handler{client: client}
}

// HandlePredict proxies the request body to the ML prediction service and
// returns its response. If the circuit breaker is open a fallback ETA is
// returned transparently.
func (handler *Handler) HandlePredict(ctx *fiber.Ctx) error {
	body := ctx.Body()
	if len(body) == 0 {
		return ctx.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "empty request body"})
	}
	requestID := middleware.RequestID(ctx)

	var request PredictRequest
	if err := ctx.BodyParser(&request); err != nil {
		return ctx.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "invalid JSON body"})
	}

	if len(request.Items) == 0 {
		return ctx.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "items array required"})
	}

	log.Printf("HTTP POST /predict items=%d request_id=%s", len(request.Items), requestID)

	result, err := handler.client.Predict(ctx.UserContext(), body)
	if err != nil {
		log.Printf("predictor: error request_id=%s: %v", requestID, err)
		return ctx.Status(fiber.StatusBadGateway).JSON(fiber.Map{"error": "prediction service unavailable"})
	}

	ctx.Set("Content-Type", "application/json")
	return ctx.Status(fiber.StatusOK).Send(result)
}
