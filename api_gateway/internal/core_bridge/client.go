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

type TCPClient struct {
	addr     string
	sourceID string
	password string

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
		tc.conn.Close()
		return fmt.Errorf("core_bridge: send auth ack: %w", err)
	}

	tc.healthy = true
	tc.conn.SetDeadline(time.Time{})
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

	for {
		resp, err := tc.readFrame()
		if err != nil {
			tc.healthy = false
			return Envelope{}, fmt.Errorf("core_bridge: read: %w", err)
		}

		// Only accept COMMAND_RESPONSE as the actual reply; skip ACKs
		// and any unsolicited server-pushed messages (e.g. emergency alerts)
		// that may arrive on the same connection.
		if resp.MsgType != MsgGatewayResponse {
			if resp.MsgType != MsgServerACK {
				log.Printf("core_bridge: skipping unsolicited %s while waiting for COMMAND_RESPONSE", resp.MsgType)
			}
			continue
		}

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

// SendKeepalive writes a keepalive frame and reads the single ACK back.
// Unlike Send(), it does not loop waiting for a non-ACK response.
func (tc *TCPClient) SendKeepalive(ctx context.Context, msg Envelope) error {
	tc.mu.Lock()
	defer tc.mu.Unlock()

	if tc.conn == nil {
		return fmt.Errorf("core_bridge: not connected")
	}

	if deadline, ok := ctx.Deadline(); ok {
		tc.conn.SetDeadline(deadline)
		defer tc.conn.SetDeadline(time.Time{})
	}

	msg.Timestamp = Now()
	msg.Checksum = computeChecksum(msg.MsgType, msg.SourceID)

	if err := tc.writeFrame(msg); err != nil {
		tc.healthy = false
		return fmt.Errorf("core_bridge: write: %w", err)
	}

	_, err := tc.readFrame()
	if err != nil {
		tc.healthy = false
		return fmt.Errorf("core_bridge: read: %w", err)
	}

	return nil
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

// writeFrame serialises and logs one protocol frame before sending it.
func (tc *TCPClient) writeFrame(env Envelope) error {
	data, _ := json.Marshal(env)
	tc.logger.Printf("TCP >>> %s", string(data))
	return writeFrameTo(tc.conn, env)
}

// readFrame reads and logs one protocol frame from the core connection.
func (tc *TCPClient) readFrame() (Envelope, error) {
	env, err := readFrameFrom(tc.conn)
	if err == nil {
		data, _ := json.Marshal(env)
		tc.logger.Printf("TCP <<< %s", string(data))
	}
	return env, err
}

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
