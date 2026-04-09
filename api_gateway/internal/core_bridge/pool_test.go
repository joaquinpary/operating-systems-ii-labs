package core_bridge

import (
	"context"
	"net"
	"sync/atomic"
	"testing"
	"time"
)

// fakeMultiServer accepts up to maxConns concurrent TCP connections,
// each one behaving like fakeCoreServer (auth + echo).
func fakeMultiServer(t *testing.T, maxConns int) net.Listener {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}

	for i := 0; i < maxConns; i++ {
		go func() {
			conn, err := ln.Accept()
			if err != nil {
				return
			}
			defer conn.Close()

			for {
				env, err := readFrameFromConn(conn)
				if err != nil {
					return
				}

				switch env.MsgType {
				case MsgAuthRequest:
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

				case MsgKeepAlive:

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

					resp := Envelope{
						MsgType:    MsgGatewayResponse,
						SourceRole: RoleServer,
						SourceID:   "SERVER",
						TargetRole: RoleGateway,
						TargetID:   env.SourceID,
						Timestamp:  Now(),
						Payload:    Payload{Message: "ALIVE"},
					}
					resp.Checksum = computeChecksum(resp.MsgType, resp.SourceID)
					writeFrameToConn(conn, resp)

					readFrameFromConn(conn)

				default:

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

					echo := env
					echo.MsgType = MsgGatewayResponse
					echo.SourceRole = RoleServer
					echo.SourceID = "SERVER"
					echo.TargetRole = RoleGateway
					echo.TargetID = env.SourceID
					echo.Timestamp = Now()
					echo.Checksum = computeChecksum(echo.MsgType, echo.SourceID)
					writeFrameToConn(conn, echo)

					readFrameFromConn(conn)
				}
			}
		}()
	}

	return ln
}

func newTestPool(t *testing.T, addr string, size int) *Pool {
	t.Helper()
	return NewPool(PoolConfig{
		Addr:         addr,
		SourceID:     "api_gateway",
		PasswordMD5:  "d41d8cd98f00b204e9800998ecf8427e",
		Size:         size,
		ConnTimeout:  5 * time.Second,
		KeepaliveIvl: 24 * time.Hour,
	})
}

func TestPool_OpenAndSend(t *testing.T) {
	const poolSize = 3
	ln := fakeMultiServer(t, poolSize)
	defer ln.Close()

	pool := newTestPool(t, ln.Addr().String(), poolSize)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	if err := pool.Open(ctx); err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer pool.Close()

	if !pool.Healthy() {
		t.Fatal("expected pool to be healthy after Open")
	}

	req := Envelope{
		MsgType:    MsgGatewayCommand,
		SourceRole: RoleGateway,
		SourceID:   "api_gateway",
		TargetRole: RoleServer,
		TargetID:   "SERVER",
		Payload:    Payload{Command: "ping"},
	}

	resp, err := pool.Send(ctx, req)
	if err != nil {
		t.Fatalf("Send: %v", err)
	}
	if resp.MsgType != MsgGatewayResponse {
		t.Errorf("expected %s, got %s", MsgGatewayResponse, resp.MsgType)
	}
}

func TestPool_ConcurrentSend(t *testing.T) {
	const poolSize = 3
	const numRequests = 10

	ln := fakeMultiServer(t, poolSize)
	defer ln.Close()

	pool := newTestPool(t, ln.Addr().String(), poolSize)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	if err := pool.Open(ctx); err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer pool.Close()

	var success atomic.Int32
	errs := make(chan error, numRequests)

	for i := 0; i < numRequests; i++ {
		go func() {
			req := Envelope{
				MsgType:    MsgGatewayCommand,
				SourceRole: RoleGateway,
				SourceID:   "api_gateway",
				TargetRole: RoleServer,
				TargetID:   "SERVER",
				Payload:    Payload{Command: "ping"},
			}
			_, err := pool.Send(ctx, req)
			if err != nil {
				errs <- err
				return
			}
			success.Add(1)
			errs <- nil
		}()
	}

	for i := 0; i < numRequests; i++ {
		if err := <-errs; err != nil {
			t.Errorf("request %d: %v", i, err)
		}
	}

	if got := success.Load(); got != numRequests {
		t.Errorf("expected %d successful sends, got %d", numRequests, got)
	}
}

func TestPool_Keepalive(t *testing.T) {
	const poolSize = 2
	ln := fakeMultiServer(t, poolSize)
	defer ln.Close()

	pool := NewPool(PoolConfig{
		Addr:         ln.Addr().String(),
		SourceID:     "api_gateway",
		PasswordMD5:  "d41d8cd98f00b204e9800998ecf8427e",
		Size:         poolSize,
		ConnTimeout:  5 * time.Second,
		KeepaliveIvl: 100 * time.Millisecond,
	})

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	if err := pool.Open(ctx); err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer pool.Close()

	time.Sleep(350 * time.Millisecond)

	if !pool.Healthy() {
		t.Fatal("expected pool to be healthy after keepalive rounds")
	}
}

func TestPool_Close(t *testing.T) {
	const poolSize = 2
	ln := fakeMultiServer(t, poolSize)
	defer ln.Close()

	pool := newTestPool(t, ln.Addr().String(), poolSize)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	if err := pool.Open(ctx); err != nil {
		t.Fatalf("Open: %v", err)
	}

	if err := pool.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}

	_, err := pool.Send(ctx, Envelope{
		MsgType:    MsgGatewayCommand,
		SourceRole: RoleGateway,
		SourceID:   "api_gateway",
		TargetRole: RoleServer,
		TargetID:   "SERVER",
		Payload:    Payload{Command: "ping"},
	})
	if err == nil {
		t.Fatal("expected error after Close, got nil")
	}
}

func TestPool_OpenPartialFailure(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	defer ln.Close()

	pool := NewPool(PoolConfig{
		Addr:         ln.Addr().String(),
		SourceID:     "api_gateway",
		PasswordMD5:  "d41d8cd98f00b204e9800998ecf8427e",
		Size:         2,
		ConnTimeout:  200 * time.Millisecond,
		KeepaliveIvl: 24 * time.Hour,
	})

	ctx, cancel := context.WithTimeout(context.Background(), 1*time.Second)
	defer cancel()

	err = pool.Open(ctx)
	if err == nil {
		pool.Close()
		t.Fatal("expected Open to fail when server doesn't respond")
	}
}
