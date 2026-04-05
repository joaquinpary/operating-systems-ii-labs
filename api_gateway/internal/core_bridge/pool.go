package core_bridge

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"sync"
	"time"
)

// Pool manages a fixed-size set of authenticated TCP connections to the C++
// core. It implements the Bridge interface so callers can use it as a
// drop-in replacement for a single TCPClient.
//
// Connections are kept alive by a background goroutine that periodically sends
// GATEWAY_TO_SERVER__KEEPALIVE messages on each idle connection.
type Pool struct {
	addr     string
	sourceID string
	password string

	size         int
	connTimeout  time.Duration
	keepaliveIvl time.Duration

	conns  chan *TCPClient // buffered channel acts as a thread-safe queue
	logger *log.Logger

	closeOnce sync.Once
	done      chan struct{} // closed by Close() to stop the keepalive loop
}

// PoolConfig groups the knobs needed by NewPool.
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
			// Close already-opened connections on failure.
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
		// Connection is broken — attempt reconnect in background, don't
		// return the dead conn to the pool.
		go p.replaceConn(c)
		return Envelope{}, err
	}

	p.put(c)
	return resp, nil
}

// Healthy returns true if at least one pooled connection is healthy.
func (p *Pool) Healthy() bool {
	// Peek at the channel non-destructively via a snapshot.
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

// Query is a stub that returns a placeholder shipment status.
// It will be replaced with a real TCP query to the C++ core.
func (p *Pool) Query(_ context.Context, shipmentID string) (Message, error) {
	payload, err := json.Marshal(map[string]string{
		"shipment_id": shipmentID,
		"status":      "pending",
	})
	if err != nil {
		return Message{}, fmt.Errorf("marshal shipment status: %w", err)
	}

	return Message{
		SourceRole: RoleServer,
		TargetID:   shipmentID,
		Payload:    payload,
	}, nil
}

// --- internal helpers ---

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
		// Pool is full (shouldn't happen), discard.
		c.Close()
	}
}

// replaceConn closes the dead connection and tries to open a fresh one.
func (p *Pool) replaceConn(dead *TCPClient) {
	dead.Close()

	select {
	case <-p.done:
		return // shutting down, don't reconnect
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

	// Send uses the mutex internally, so this is safe.
	_, err := c.Send(ctx, msg)
	return err
}
