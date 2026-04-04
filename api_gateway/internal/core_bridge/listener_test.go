package core_bridge

import (
	"context"
	"net"
	"sync/atomic"
	"testing"
	"time"
)

// fakePushServer accepts one connection, authenticates it, then pushes
// numPush envelopes to the client. It reads+discards the ACKs the client
// sends back. It also handles keepalive frames from the client.
func fakePushServer(t *testing.T, numPush int) net.Listener {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}

	go func() {
		conn, err := ln.Accept()
		if err != nil {
			return
		}
		defer conn.Close()

		// --- auth handshake ---
		env, err := readFrameFromConn(conn)
		if err != nil {
			return
		}
		if env.MsgType != MsgAuthRequest {
			return
		}
		resp := Envelope{
			MsgType:    MsgAuthResponse,
			SourceRole: RoleServer,
			SourceID:   "SERVER",
			TargetRole: RoleGateway,
			TargetID:   env.SourceID,
			Timestamp:  Now(),
			Payload:    Payload{StatusCode: 200},
		}
		resp.Checksum = computeChecksum(resp.MsgType, resp.SourceID)
		writeFrameToConn(conn, resp)

		// --- push events ---
		for i := 0; i < numPush; i++ {
			push := Envelope{
				MsgType:    MsgGatewayResponse,
				SourceRole: RoleServer,
				SourceID:   "SERVER",
				TargetRole: RoleGateway,
				TargetID:   "api_gateway",
				Timestamp:  Now(),
				Payload: Payload{
					Message: "event",
					Command: "INVENTORY_UPDATE",
				},
			}
			push.Checksum = computeChecksum(push.MsgType, push.SourceID)
			writeFrameToConn(conn, push)

			// Read the ACK the listener sends back.
			readFrameFromConn(conn)
		}

		// Keep connection alive for a bit so keepalive tests can run.
		// Handle any keepalive frames that arrive.
		conn.SetReadDeadline(time.Now().Add(2 * time.Second))
		for {
			env, err := readFrameFromConn(conn)
			if err != nil {
				return
			}
			if env.MsgType == MsgKeepAlive {
				ack := Envelope{
					MsgType:    MsgServerACK,
					SourceRole: RoleServer,
					SourceID:   "SERVER",
					TargetRole: RoleGateway,
					TargetID:   env.SourceID,
					Timestamp:  Now(),
					Payload: Payload{
						StatusCode:      200,
						AckForTimestamp: env.Timestamp.UTC().Format(timestampLayout),
					},
				}
				ack.Checksum = computeChecksum(ack.MsgType, ack.SourceID)
				writeFrameToConn(conn, ack)

				alive := Envelope{
					MsgType:    MsgGatewayResponse,
					SourceRole: RoleServer,
					SourceID:   "SERVER",
					TargetRole: RoleGateway,
					TargetID:   env.SourceID,
					Timestamp:  Now(),
					Payload:    Payload{Message: "ALIVE"},
				}
				alive.Checksum = computeChecksum(alive.MsgType, alive.SourceID)
				writeFrameToConn(conn, alive)

				// read ACK for alive response
				readFrameFromConn(conn)
			}
		}
	}()

	return ln
}

func newTestListener(t *testing.T, addr string) *Listener {
	t.Helper()
	return NewListener(ListenerConfig{
		Addr:         addr,
		SourceID:     "api_gateway",
		PasswordMD5:  "d41d8cd98f00b204e9800998ecf8427e",
		ConnTimeout:  5 * time.Second,
		KeepaliveIvl: 24 * time.Hour, // disabled for most tests
	})
}

func TestListener_ReceivePushedEvents(t *testing.T) {
	const numEvents = 3
	ln := fakePushServer(t, numEvents)
	defer ln.Close()

	listener := newTestListener(t, ln.Addr().String())

	var received atomic.Int32
	listener.OnEvent(func(env Envelope) {
		received.Add(1)
	})

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	if err := listener.Start(ctx); err != nil {
		t.Fatalf("Start: %v", err)
	}
	defer listener.Close()

	// Wait for events to be dispatched.
	deadline := time.After(3 * time.Second)
	for {
		if received.Load() == int32(numEvents) {
			break
		}
		select {
		case <-deadline:
			t.Fatalf("timed out: received %d/%d events", received.Load(), numEvents)
		default:
			time.Sleep(10 * time.Millisecond)
		}
	}
}

func TestListener_Keepalive(t *testing.T) {
	ln := fakePushServer(t, 0) // no push events
	defer ln.Close()

	listener := NewListener(ListenerConfig{
		Addr:         ln.Addr().String(),
		SourceID:     "api_gateway",
		PasswordMD5:  "d41d8cd98f00b204e9800998ecf8427e",
		ConnTimeout:  5 * time.Second,
		KeepaliveIvl: 100 * time.Millisecond, // very short
	})

	// Keepalive responses ("ALIVE") should NOT reach event handlers.
	var spurious atomic.Int32
	listener.OnEvent(func(env Envelope) {
		spurious.Add(1)
	})

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	if err := listener.Start(ctx); err != nil {
		t.Fatalf("Start: %v", err)
	}
	defer listener.Close()

	// Wait for a few keepalive rounds.
	time.Sleep(350 * time.Millisecond)

	if got := spurious.Load(); got != 0 {
		t.Errorf("expected 0 spurious events from keepalive responses, got %d", got)
	}
}

func TestListener_Close(t *testing.T) {
	ln := fakePushServer(t, 0)
	defer ln.Close()

	listener := newTestListener(t, ln.Addr().String())

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	if err := listener.Start(ctx); err != nil {
		t.Fatalf("Start: %v", err)
	}

	if err := listener.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}

	// Double close should be safe.
	if err := listener.Close(); err != nil {
		t.Fatalf("second Close: %v", err)
	}
}

func TestListener_AuthFailure(t *testing.T) {
	// Server that rejects auth.
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	defer ln.Close()

	go func() {
		conn, err := ln.Accept()
		if err != nil {
			return
		}
		defer conn.Close()

		// Read auth request.
		readFrameFromConn(conn)

		// Reject.
		resp := Envelope{
			MsgType:    MsgAuthResponse,
			SourceRole: RoleServer,
			SourceID:   "SERVER",
			TargetRole: RoleGateway,
			TargetID:   "api_gateway",
			Timestamp:  Now(),
			Payload:    Payload{StatusCode: 401},
		}
		resp.Checksum = computeChecksum(resp.MsgType, resp.SourceID)
		writeFrameToConn(conn, resp)
	}()

	listener := newTestListener(t, ln.Addr().String())

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	err = listener.Start(ctx)
	if err == nil {
		listener.Close()
		t.Fatal("expected Start to fail on auth rejection")
	}
}
