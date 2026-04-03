package predictor

import "context"

type Client struct {
	baseURL string
}

func NewClient(baseURL string) *Client {
	return &Client{baseURL: baseURL}
}

func (client *Client) BaseURL() string {
	return client.baseURL
}

func (client *Client) Predict(_ context.Context, _ []byte) ([]byte, error) {
	return nil, nil
}
