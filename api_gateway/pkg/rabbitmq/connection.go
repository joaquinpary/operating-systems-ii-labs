package rabbitmq

type Connection struct {
	url string
}

func Connect(url string) (*Connection, error) {
	return &Connection{url: url}, nil
}

func (connection *Connection) URL() string {
	return connection.url
}

func (connection *Connection) Close() error {
	return nil
}
