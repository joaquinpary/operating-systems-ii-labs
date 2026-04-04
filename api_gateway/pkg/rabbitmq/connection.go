package rabbitmq

import "context"

type Connection struct {
	url string
}

func Connect(url string) (*Connection, error) {
	return &Connection{url: url}, nil
}

func (connection *Connection) URL() string {
	return connection.url
}

func (connection *Connection) Publish(_ context.Context, _ string, _ []byte) error {
	return nil
}

func (connection *Connection) Close() error {
	return nil
}
