package rabbitmq

import (
	"context"
	"fmt"
	"sync"
	"time"

	amqp "github.com/rabbitmq/amqp091-go"
)

type Connection struct {
	url string

	connection *amqp.Connection
	publisher  *amqp.Channel
	consumer   *amqp.Channel

	publishMu sync.Mutex
	consumeMu sync.Mutex
	closeOnce sync.Once
}

func Connect(url string) (*Connection, error) {
	connection, err := amqp.Dial(url)
	if err != nil {
		return nil, fmt.Errorf("dial rabbitmq: %w", err)
	}

	publisher, err := connection.Channel()
	if err != nil {
		connection.Close()
		return nil, fmt.Errorf("open rabbitmq publisher channel: %w", err)
	}

	return &Connection{
		url:        url,
		connection: connection,
		publisher:  publisher,
	}, nil
}

func (connection *Connection) URL() string {
	return connection.url

}

func (connection *Connection) Publish(ctx context.Context, queue string, body []byte) error {
	connection.publishMu.Lock()
	defer connection.publishMu.Unlock()

	if connection.publisher == nil {
		return fmt.Errorf("rabbitmq publisher channel is closed")
	}

	if _, err := declareQueue(connection.publisher, queue); err != nil {
		return err
	}

	if err := connection.publisher.PublishWithContext(ctx, "", queue, false, false, amqp.Publishing{
		ContentType:  "application/json",
		DeliveryMode: amqp.Persistent,
		Timestamp:    time.Now().UTC(),
		Body:         body,
	}); err != nil {
		return fmt.Errorf("publish to queue %q: %w", queue, err)
	}

	return nil
}

func (connection *Connection) Consume(ctx context.Context, queue string) (<-chan amqp.Delivery, error) {
	connection.consumeMu.Lock()
	defer connection.consumeMu.Unlock()

	if connection.connection == nil {
		return nil, fmt.Errorf("rabbitmq connection is closed")
	}

	if connection.consumer != nil {
		return nil, fmt.Errorf("rabbitmq consumer already started")
	}

	consumer, err := connection.connection.Channel()
	if err != nil {
		return nil, fmt.Errorf("open rabbitmq consumer channel: %w", err)
	}

	if _, err := declareQueue(consumer, queue); err != nil {
		consumer.Close()
		return nil, err
	}

	deliveries, err := consumer.Consume(queue, "", false, false, false, false, nil)
	if err != nil {
		consumer.Close()
		return nil, fmt.Errorf("consume queue %q: %w", queue, err)
	}

	connection.consumer = consumer

	go func() {
		<-ctx.Done()

		connection.consumeMu.Lock()
		defer connection.consumeMu.Unlock()

		if connection.consumer != nil {
			_ = connection.consumer.Close()
			connection.consumer = nil
		}
	}()

	return deliveries, nil
}

func (connection *Connection) Close() error {
	var closeErr error

	connection.closeOnce.Do(func() {
		connection.consumeMu.Lock()
		if connection.consumer != nil {
			if err := connection.consumer.Close(); err != nil && closeErr == nil {
				closeErr = fmt.Errorf("close rabbitmq consumer channel: %w", err)
			}
			connection.consumer = nil
		}
		connection.consumeMu.Unlock()

		connection.publishMu.Lock()
		if connection.publisher != nil {
			if err := connection.publisher.Close(); err != nil && closeErr == nil {
				closeErr = fmt.Errorf("close rabbitmq publisher channel: %w", err)
			}
			connection.publisher = nil
		}
		connection.publishMu.Unlock()

		if connection.connection != nil {
			if err := connection.connection.Close(); err != nil && closeErr == nil {
				closeErr = fmt.Errorf("close rabbitmq connection: %w", err)
			}
			connection.connection = nil
		}
	})

	return closeErr
}

func declareQueue(channel *amqp.Channel, queue string) (amqp.Queue, error) {
	declared, err := channel.QueueDeclare(queue, true, false, false, false, nil)
	if err != nil {
		return amqp.Queue{}, fmt.Errorf("declare queue %q: %w", queue, err)
	}

	return declared, nil
}
