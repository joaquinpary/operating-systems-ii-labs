package core_bridge

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"sync"
	"time"
)

// TCPClient is the adapter that implements Bridge over a persistent TCP
// connection to the C++ logistics core. It handles the 1024-byte fixed-frame
// protocol, DJB2 checksum generation, and the initial AUTH handshake.
type TCPClient struct {
	addr     string
	sourceID string
	password string // MD5 hex of the plain-text password.

	mu      sync.Mutex
	conn    net.Conn
	healthy bool
	logger  *log.Logger
}

// NewTCPClient returns an uninitialised TCPClient. Call Connect() to open the
// connection and authenticate before sending any messages.
func NewTCPClient(addr, sourceID, passwordMD5 string, logger *log.Logger) *TCPClient {
	if logger == nil {
		logger = log.Default()
	}
	return &TCPClient{
		addr:     addr,
		sourceID: sourceID,
		password: passwordMD5,
		logger:   logger,
	}
}

// Connect dials the C++ core and performs the mandatory AUTH_REQUEST handshake.
func (tc *TCPClient) Connect(ctx context.Context) error {
	tc.mu.Lock()
	defer tc.mu.Unlock()

	dialer := net.Dialer{}
	conn, err := dialer.DialContext(ctx, "tcp", tc.addr)
	if err != nil {
		return fmt.Errorf("core_bridge: dial %s: %w", tc.addr, err)
	}
	tc.conn = conn

	// Propagate the context deadline so readFrame/writeFrame don't block
	// forever if the server never responds to the AUTH handshake.
	if deadline, ok := ctx.Deadline(); ok {
		tc.conn.SetDeadline(deadline)
	}

	authEnv := Envelope{
		MsgType:    MsgAuthRequest,
		SourceRole: RoleGateway,
		SourceID:   tc.sourceID,
		TargetRole: RoleServer,
		TargetID:   "SERVER",
		Timestamp:  Now(),
		Payload: Payload{
			Username: tc.sourceID,
			Password: tc.password,
		},
	}
	authEnv.Checksum = computeChecksum(authEnv.MsgType, authEnv.SourceID)

	if err := tc.writeFrame(authEnv); err != nil {
		tc.conn.Close()
		return fmt.Errorf("core_bridge: send auth: %w", err)
	}

	resp, err := tc.readFrame()
	if err != nil {
		tc.conn.Close()
		return fmt.Errorf("core_bridge: read auth response: %w", err)
	}

	if resp.MsgType != MsgAuthResponse || resp.Payload.StatusCode != 200 {
		tc.conn.Close()
		return fmt.Errorf("core_bridge: auth rejected (status %d)", resp.Payload.StatusCode)
	}

	tc.healthy = true
	tc.conn.SetDeadline(time.Time{}) // clear auth deadline
	tc.logger.Printf("core_bridge: authenticated to %s as %s", tc.addr, tc.sourceID)
	return nil
}

// Send transmits msg and returns the first non-ACK response from the core.
// It is safe for concurrent use; a mutex serialises access to the socket.
func (tc *TCPClient) Send(ctx context.Context, msg Envelope) (Envelope, error) {
	tc.mu.Lock()
	defer tc.mu.Unlock()

	if tc.conn == nil {
		return Envelope{}, fmt.Errorf("core_bridge: not connected")
	}

	if deadline, ok := ctx.Deadline(); ok {
		tc.conn.SetDeadline(deadline)
		defer tc.conn.SetDeadline(time.Time{})
	}

	msg.Timestamp = Now()
	msg.Checksum = computeChecksum(msg.MsgType, msg.SourceID)

	if err := tc.writeFrame(msg); err != nil {
		tc.healthy = false
		return Envelope{}, fmt.Errorf("core_bridge: write: %w", err)
	}

	// The C++ core always sends an ACK first, followed by the real response.
	// Read frames until we get a non-ACK response.
	for {
		resp, err := tc.readFrame()
		if err != nil {
			tc.healthy = false
			return Envelope{}, fmt.Errorf("core_bridge: read: %w", err)
		}

		// If core sent us an ACK, send our ACK back and keep reading.
		if resp.MsgType == MsgServerACK {
			continue
		}

		// Send ACK for the actual response.
		ack := Envelope{
			MsgType:    MsgACK,
			SourceRole: RoleGateway,
			SourceID:   tc.sourceID,
			TargetRole: RoleServer,
			TargetID:   "SERVER",
			Timestamp:  Now(),
			Payload: Payload{
				StatusCode:      200,
				AckForTimestamp: resp.Timestamp.UTC().Format(timestampLayout),
			},
		}
		ack.Checksum = computeChecksum(ack.MsgType, ack.SourceID)
		if err := tc.writeFrame(ack); err != nil {
			tc.healthy = false
			return Envelope{}, fmt.Errorf("core_bridge: write ack: %w", err)
		}

		return resp, nil
	}
}

// Close shuts down the TCP connection.
func (tc *TCPClient) Close() error {
	tc.mu.Lock()
	defer tc.mu.Unlock()

	tc.healthy = false
	if tc.conn != nil {
		return tc.conn.Close()
	}
	return nil
}

// Healthy reports whether the connection is considered alive.
func (tc *TCPClient) Healthy() bool {
	tc.mu.Lock()
	defer tc.mu.Unlock()
	return tc.healthy
}

func (tc *TCPClient) writeFrame(env Envelope) error { return writeFrameTo(tc.conn, env) }
func (tc *TCPClient) readFrame() (Envelope, error)  { return readFrameFrom(tc.conn) }

// --- package-level frame I/O (used by TCPClient and Listener) ---

// writeFrameTo serialises env to JSON and writes it in a zero-padded 1024-byte frame.
func writeFrameTo(conn net.Conn, env Envelope) error {
	data, err := json.Marshal(env)
	if err != nil {
		return fmt.Errorf("marshal: %w", err)
	}
	if len(data) > FrameSize {
		return fmt.Errorf("payload exceeds %d-byte frame (%d bytes)", FrameSize, len(data))
	}

	var frame [FrameSize]byte
	copy(frame[:], data)

	_, err = conn.Write(frame[:])
	return err
}

// readFrameFrom reads exactly 1024 bytes from conn and deserialises the
// JSON envelope contained within.
func readFrameFrom(conn net.Conn) (Envelope, error) {
	var frame [FrameSize]byte
	n := 0
	for n < FrameSize {
		read, err := conn.Read(frame[n:])
		if err != nil {
			return Envelope{}, fmt.Errorf("read: %w", err)
		}
		n += read
	}

	// Trim zero-padding before unmarshalling.
	end := FrameSize
	for end > 0 && frame[end-1] == 0 {
		end--
	}

	var env Envelope
	if err := json.Unmarshal(frame[:end], &env); err != nil {
		return Envelope{}, fmt.Errorf("unmarshal: %w", err)
	}
	return env, nil
}
