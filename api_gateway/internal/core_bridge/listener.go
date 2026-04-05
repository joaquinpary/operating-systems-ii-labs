package core_bridge

import (
	"context"
	"fmt"
	"log"
	"net"
	"sync"
	"time"
)

// EventHandler is invoked by the Listener for every pushed frame that is
// not an ACK or keepalive response.
type EventHandler func(Envelope)

// Listener maintains a dedicated TCP connection to the C++ core that
// continuously reads server-pushed events (inventory updates, emergency
// alerts, etc.). It authenticates LAST — after the Pool — so its session
// is the one indexed by username in the C++ session_manager reverse-map.
//
// Internally it runs two goroutines:
//   - readLoop:      blocking reads → ACK + dispatch to registered handlers
//   - keepaliveLoop: periodic KEEPALIVE writes to avoid session timeout
//
// Go's net.Conn supports concurrent Read and Write, so no read-mutex is
// needed; only writes (ACKs + keepalives) are serialised with writeMu.
type Listener struct {
	addr     string
	sourceID string
	password string

	conn         net.Conn
	writeMu      sync.Mutex
	handlers     []EventHandler
	handlersMu   sync.RWMutex
	logger       *log.Logger
	done         chan struct{}
	closeOnce    sync.Once
	keepaliveIvl time.Duration
	connTimeout  time.Duration
}

// ListenerConfig groups the knobs for NewListener.
type ListenerConfig struct {
	Addr         string
	SourceID     string
	PasswordMD5  string
	ConnTimeout  time.Duration
	KeepaliveIvl time.Duration
	Logger       *log.Logger
}

// NewListener creates a Listener but does NOT connect yet. Call Start().
func NewListener(cfg ListenerConfig) *Listener {
	if cfg.Logger == nil {
		cfg.Logger = log.Default()
	}
	return &Listener{
		addr:         cfg.Addr,
		sourceID:     cfg.SourceID,
		password:     cfg.PasswordMD5,
		logger:       cfg.Logger,
		done:         make(chan struct{}),
		keepaliveIvl: cfg.KeepaliveIvl,
		connTimeout:  cfg.ConnTimeout,
	}
}

// OnEvent registers a handler that will be called for every non-ACK,
// non-keepalive frame pushed by the C++ core. Safe to call before Start().
func (l *Listener) OnEvent(h EventHandler) {
	l.handlersMu.Lock()
	l.handlers = append(l.handlers, h)
	l.handlersMu.Unlock()
}

// Start dials the C++ core, authenticates, and launches the background
// read + keepalive goroutines. It blocks until the handshake succeeds.
func (l *Listener) Start(ctx context.Context) error {
	dialCtx, cancel := context.WithTimeout(ctx, l.connTimeout)
	defer cancel()

	dialer := net.Dialer{}
	conn, err := dialer.DialContext(dialCtx, "tcp", l.addr)
	if err != nil {
		return fmt.Errorf("listener: dial %s: %w", l.addr, err)
	}
	l.conn = conn

	if err := l.authenticate(dialCtx); err != nil {
		l.conn.Close()
		return err
	}

	l.logger.Printf("listener: authenticated to %s as %s", l.addr, l.sourceID)

	go l.readLoop()
	go l.keepaliveLoop()

	return nil
}

// Close stops both goroutines and closes the TCP connection.
func (l *Listener) Close() error {
	var err error
	l.closeOnce.Do(func() {
		close(l.done)
		if l.conn != nil {
			err = l.conn.Close()
		}
	})
	return err
}

// --- auth ---

func (l *Listener) authenticate(ctx context.Context) error {
	if deadline, ok := ctx.Deadline(); ok {
		l.conn.SetDeadline(deadline)
		defer l.conn.SetDeadline(time.Time{})
	}

	auth := Envelope{
		MsgType:    MsgAuthRequest,
		SourceRole: RoleGateway,
		SourceID:   l.sourceID,
		TargetRole: RoleServer,
		TargetID:   "SERVER",
		Timestamp:  Now(),
		Payload: Payload{
			Username: l.sourceID,
			Password: l.password,
		},
	}
	auth.Checksum = computeChecksum(auth.MsgType, auth.SourceID)

	if err := writeFrameTo(l.conn, auth); err != nil {
		return fmt.Errorf("listener: send auth: %w", err)
	}
	resp, err := readFrameFrom(l.conn)
	if err != nil {
		return fmt.Errorf("listener: read auth response: %w", err)
	}
	if resp.MsgType != MsgAuthResponse || resp.Payload.StatusCode != 200 {
		return fmt.Errorf("listener: auth rejected (status %d)", resp.Payload.StatusCode)
	}
	return nil
}

// --- read loop ---

// readLoop spawns a blocking reader goroutine that feeds frames into a
// channel. The main select dispatches events while the done channel allows
// graceful shutdown.
func (l *Listener) readLoop() {
	frames := make(chan Envelope)
	errs := make(chan error, 1)

	// Blocking reader — runs until conn is closed.
	go func() {
		for {
			env, err := readFrameFrom(l.conn)
			if err != nil {
				errs <- err
				return
			}
			frames <- env
		}
	}()

	for {
		select {
		case <-l.done:
			return
		case err := <-errs:
			select {
			case <-l.done:
				// Expected: Close() was called, conn.Read returned error.
			default:
				l.logger.Printf("listener: read error: %v", err)
			}
			return
		case env := <-frames:
			l.processFrame(env)
		}
	}
}

func (l *Listener) processFrame(env Envelope) {
	// Server ACKs (for our keepalives) are silently consumed.
	if env.MsgType == MsgServerACK {
		return
	}

	// Send ACK for every non-ACK frame.
	l.sendACK(env)

	// Keepalive responses are harmless noise — don't bubble to handlers.
	if env.MsgType == MsgGatewayResponse && env.Payload.Message == "ALIVE" {
		return
	}

	l.dispatch(env)
}

func (l *Listener) sendACK(env Envelope) {
	ack := Envelope{
		MsgType:    MsgACK,
		SourceRole: RoleGateway,
		SourceID:   l.sourceID,
		TargetRole: RoleServer,
		TargetID:   "SERVER",
		Timestamp:  Now(),
		Payload: Payload{
			StatusCode:      200,
			AckForTimestamp: env.Timestamp.UTC().Format(timestampLayout),
		},
	}
	ack.Checksum = computeChecksum(ack.MsgType, ack.SourceID)

	l.writeMu.Lock()
	writeFrameTo(l.conn, ack)
	l.writeMu.Unlock()
}

func (l *Listener) dispatch(env Envelope) {
	l.handlersMu.RLock()
	defer l.handlersMu.RUnlock()
	for _, h := range l.handlers {
		h(env)
	}
}

// --- keepalive loop ---

func (l *Listener) keepaliveLoop() {
	ticker := time.NewTicker(l.keepaliveIvl)
	defer ticker.Stop()

	for {
		select {
		case <-l.done:
			return
		case <-ticker.C:
			l.sendKeepalive()
		}
	}
}

func (l *Listener) sendKeepalive() {
	msg := Envelope{
		MsgType:    MsgKeepAlive,
		SourceRole: RoleGateway,
		SourceID:   l.sourceID,
		TargetRole: RoleServer,
		TargetID:   "SERVER",
		Timestamp:  Now(),
		Payload:    Payload{Message: "ALIVE"},
	}
	msg.Checksum = computeChecksum(msg.MsgType, msg.SourceID)

	l.writeMu.Lock()
	writeFrameTo(l.conn, msg)
	l.writeMu.Unlock()
}
