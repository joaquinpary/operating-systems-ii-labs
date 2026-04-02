package chat

import "context"

type Service interface {
	SendMessage(ctx context.Context, message Message) (Message, error)
	ListMessages(ctx context.Context, targetID string) ([]Message, error)
}

type Transport interface {
	PushMessage(ctx context.Context, message Message) error
	PullMessages(ctx context.Context, targetID string) ([]Message, error)
}
