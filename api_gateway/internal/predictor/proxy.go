package predictor

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"time"
)

const (
	defaultTimeout      = 5 * time.Second
	defaultMaxFailures  = 3
	defaultResetTimeout = 30 * time.Second
)

type PredictRequest struct {
	Items []PredictItem `json:"items"`
}

type PredictItem struct {
	ItemID   int `json:"item_id"`
	Quantity int `json:"quantity"`
}

type PredictResponse struct {
	ETAHours   int     `json:"eta_hours"`
	BoxSize    string  `json:"box_size"`
	Confidence float64 `json:"confidence"`
	Fallback   bool    `json:"fallback,omitempty"`
}

type Client struct {
	baseURL    string
	httpClient *http.Client
	breaker    *CircuitBreaker
}

// NewClient builds the ML prediction service HTTP client.
func NewClient(baseURL string) *Client {
	return &Client{
		baseURL: baseURL,
		httpClient: &http.Client{
			Timeout: defaultTimeout,
		},
		breaker: NewCircuitBreaker(defaultMaxFailures, defaultResetTimeout),
	}
}

// BaseURL returns the configured ML service base URL.
func (client *Client) BaseURL() string {
	return client.baseURL
}

var fallbackResponse = PredictResponse{
	ETAHours:   48,
	BoxSize:    "standard",
	Confidence: 0,
	Fallback:   true,
}

// Predict sends the request body to the ML service, wrapped in the circuit
// breaker. When the breaker is open it returns a hardcoded fallback ETA.
func (client *Client) Predict(ctx context.Context, body []byte) ([]byte, error) {
	result, err := client.breaker.Execute(func() ([]byte, error) {
		return client.doPost(ctx, body)
	})

	if err == ErrBreakerOpen {
		log.Printf("predictor: circuit breaker open, returning fallback")
		return json.Marshal(fallbackResponse)
	}

	return result, err
}

// doPost sends one prediction request to the configured ML service.
func (client *Client) doPost(ctx context.Context, body []byte) ([]byte, error) {
	request, err := http.NewRequestWithContext(ctx, http.MethodPost, client.baseURL+"/predict", bytes.NewReader(body))
	if err != nil {
		return nil, fmt.Errorf("predictor: build request: %w", err)
	}
	request.Header.Set("Content-Type", "application/json")

	response, err := client.httpClient.Do(request)
	if err != nil {
		return nil, fmt.Errorf("predictor: request failed: %w", err)
	}
	defer response.Body.Close()

	responseBody, err := io.ReadAll(response.Body)
	if err != nil {
		return nil, fmt.Errorf("predictor: read response: %w", err)
	}

	if response.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("predictor: status %d: %s", response.StatusCode, string(responseBody))
	}

	return responseBody, nil
}
