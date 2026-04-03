package core_bridge

import "fmt"

type Pool struct {
	host string
	port int
}

func NewPool(host string, port int) *Pool {
	return &Pool{
		host: host,
		port: port,
	}
}

func (pool *Pool) Address() string {
	return fmt.Sprintf("%s:%d", pool.host, pool.port)
}

func (pool *Pool) Send(_ Message) error {
	return nil
}

func (pool *Pool) Close() error {
	return nil
}
