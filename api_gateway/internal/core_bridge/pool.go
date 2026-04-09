package core_bridge

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"sync"
	"time"
)

type Pool struct {
	addr     string
	sourceID string
	password string

	size         int
	connTimeout  time.Duration
	keepaliveIvl time.Duration

	conns  chan *TCPClient
	logger *log.Logger

	closeOnce sync.Once
	done      chan struct{}
}

type PoolConfig struct {
	Addr         string
	SourceID     string
	PasswordMD5  string
	Size         int
	ConnTimeout  time.Duration
	KeepaliveIvl time.Duration
	Logger       *log.Logger
}

// NewPool creates a pool but does NOT open any connections yet.
// Call Open() to dial and authenticate all pool members.
func NewPool(cfg PoolConfig) *Pool {
	if cfg.Logger == nil {
		cfg.Logger = log.Default()
	}
	if cfg.Size < 1 {
		cfg.Size = 1
	}
	return &Pool{
		addr:         cfg.Addr,
		sourceID:     cfg.SourceID,
		password:     cfg.PasswordMD5,
		size:         cfg.Size,
		connTimeout:  cfg.ConnTimeout,
		keepaliveIvl: cfg.KeepaliveIvl,
		conns:        make(chan *TCPClient, cfg.Size),
		logger:       cfg.Logger,
		done:         make(chan struct{}),
	}
}

// Open dials and authenticates cfg.Size connections, then starts the
// background keepalive goroutine. It is an error to call Open twice.
func (p *Pool) Open(ctx context.Context) error {
	for i := 0; i < p.size; i++ {
		c, err := p.dial(ctx)
		if err != nil {
			p.drain()
			return fmt.Errorf("pool: open conn %d/%d: %w", i+1, p.size, err)
		}
		p.conns <- c
	}
	p.logger.Printf("pool: opened %d connections to %s", p.size, p.addr)
	go p.keepaliveLoop()
	return nil
}

// Send implements Bridge. It borrows a connection from the pool, sends the
// message, and returns the connection afterwards.
func (p *Pool) Send(ctx context.Context, msg Envelope) (Envelope, error) {
	c, err := p.get(ctx)
	if err != nil {
		return Envelope{}, err
	}

	resp, err := c.Send(ctx, msg)
	if err != nil {
		go p.replaceConn(c)
		return Envelope{}, err
	}

	p.put(c)
	return resp, nil
}

// Healthy returns true if at least one pooled connection is healthy.
func (p *Pool) Healthy() bool {
	n := len(p.conns)
	for i := 0; i < n; i++ {
		select {
		case c := <-p.conns:
			healthy := c.Healthy()
			p.conns <- c
			if healthy {
				return true
			}
		default:
			return false
		}
	}
	return false
}

// Close shuts down the keepalive goroutine and closes every connection.
func (p *Pool) Close() error {
	var firstErr error
	p.closeOnce.Do(func() {
		close(p.done)
		firstErr = p.drain()
	})
	return firstErr
}

// Command sends a GATEWAY_TO_SERVER__COMMAND to the C++ core via a pooled
// connection and returns the response envelope.
func (p *Pool) Command(ctx context.Context, command string, payload Payload) (Envelope, error) {
	payload.Command = command
	env := Envelope{
		MsgType:    MsgGatewayCommand,
		SourceRole: RoleGateway,
		SourceID:   p.sourceID,
		TargetRole: RoleServer,
		TargetID:   "SERVER",
		Timestamp:  Now(),
		Payload:    payload,
	}
	return p.Send(ctx, env)
}

// Query fetches the shipment status response from the C++ core.
func (p *Pool) Query(ctx context.Context, shipmentID string) (Message, error) {
	response, err := p.Command(ctx, "get_shipment_status", Payload{
		Args: shipmentID,
	})
	if err != nil {
		return Message{}, fmt.Errorf("query shipment status: %w", err)
	}

	payload, err := json.Marshal(response.Payload)
	if err != nil {
		return Message{}, fmt.Errorf("marshal shipment status response: %w", err)
	}

	return Message{
		MsgType:    response.MsgType,
		SourceRole: response.SourceRole,
		SourceID:   response.SourceID,
		TargetRole: response.TargetRole,
		TargetID:   response.TargetID,
		Timestamp:  response.Timestamp.UTC().Format(timestampLayout),
		Payload:    payload,
		Checksum:   response.Checksum,
	}, nil
}

// SourceID returns the gateway identifier used by this connection pool.
func (p *Pool) SourceID() string {
	return p.sourceID
}

// dial creates and authenticates one TCPClient.
func (p *Pool) dial(ctx context.Context) (*TCPClient, error) {
	dialCtx, cancel := context.WithTimeout(ctx, p.connTimeout)
	defer cancel()

	c := NewTCPClient(p.addr, p.sourceID, p.password, p.logger)
	if err := c.Connect(dialCtx); err != nil {
		return nil, err
	}
	return c, nil
}

// get blocks until a connection is available or the context expires.
func (p *Pool) get(ctx context.Context) (*TCPClient, error) {
	select {
	case c := <-p.conns:
		return c, nil
	case <-ctx.Done():
		return nil, fmt.Errorf("pool: get conn: %w", ctx.Err())
	case <-p.done:
		return nil, fmt.Errorf("pool: closed")
	}
}

// put returns a healthy connection to the pool.
func (p *Pool) put(c *TCPClient) {
	select {
	case p.conns <- c:
	default:
		c.Close()
	}
}

// replaceConn closes the dead connection and tries to open a fresh one.
func (p *Pool) replaceConn(dead *TCPClient) {
	dead.Close()

	select {
	case <-p.done:
		return
	default:
	}

	ctx, cancel := context.WithTimeout(context.Background(), p.connTimeout)
	defer cancel()

	c, err := p.dial(ctx)
	if err != nil {
		p.logger.Printf("pool: reconnect failed: %v", err)
		return
	}
	p.put(c)
	p.logger.Printf("pool: reconnected to %s", p.addr)
}

// drain closes every connection currently sitting in the channel.
func (p *Pool) drain() error {
	var firstErr error
	for {
		select {
		case c := <-p.conns:
			if err := c.Close(); err != nil && firstErr == nil {
				firstErr = err
			}
		default:
			return firstErr
		}
	}
}

// keepaliveLoop runs in the background, periodically sending a KEEPALIVE
// message on each idle connection. It exits when p.done is closed.
func (p *Pool) keepaliveLoop() {
	ticker := time.NewTicker(p.keepaliveIvl)
	defer ticker.Stop()

	for {
		select {
		case <-p.done:
			return
		case <-ticker.C:
			p.sendKeepalives()
		}
	}
}

// sendKeepalives drains the pool, sends a keepalive on each connection,
// and puts healthy connections back.
func (p *Pool) sendKeepalives() {
	n := len(p.conns)
	for i := 0; i < n; i++ {
		select {
		case c := <-p.conns:
			if err := p.sendOneKeepalive(c); err != nil {
				p.logger.Printf("pool: keepalive failed: %v", err)
				go p.replaceConn(c)
			} else {
				p.conns <- c
			}
		default:
			return
		}
	}
}

// sendOneKeepalive sends a single KEEPALIVE frame and reads the ACK.
// Unlike Send(), it does not wait for a non-ACK response because the server
// only replies with an ACK to keepalives.
func (p *Pool) sendOneKeepalive(c *TCPClient) error {
	ctx, cancel := context.WithTimeout(context.Background(), p.connTimeout)
	defer cancel()

	msg := Envelope{
		MsgType:    MsgKeepAlive,
		SourceRole: RoleGateway,
		SourceID:   p.sourceID,
		TargetRole: RoleServer,
		TargetID:   "SERVER",
		Payload:    Payload{Message: "ALIVE"},
	}

	return c.SendKeepalive(ctx, msg)
}
