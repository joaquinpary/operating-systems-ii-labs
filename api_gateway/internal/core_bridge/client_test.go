package core_bridge

import (
	"context"
	"encoding/json"
	"net"
	"testing"
	"time"
)

// fakeCoreServer spins up a TCP listener that mimics the C++ core's
// fixed-frame protocol: it reads 1024-byte frames, responds with an
// AUTH_RESPONSE, and for subsequent messages it replies with an ACK
// followed by an echo of the received envelope.
func fakeCoreServer(t *testing.T, addr string) net.Listener {
	t.Helper()
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		t.Fatalf("listen %s: %v", addr, err)
	}

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

	return ln
}

func readFrameFromConn(conn net.Conn) (Envelope, error) {
	var frame [FrameSize]byte
	n := 0
	for n < FrameSize {
		read, err := conn.Read(frame[n:])
		if err != nil {
			return Envelope{}, err
		}
		n += read
	}
	end := FrameSize
	for end > 0 && frame[end-1] == 0 {
		end--
	}
	var env Envelope
	if err := json.Unmarshal(frame[:end], &env); err != nil {
		return Envelope{}, err
	}
	return env, nil
}

func writeFrameToConn(conn net.Conn, env Envelope) error {
	data, _ := json.Marshal(env)
	var frame [FrameSize]byte
	copy(frame[:], data)
	_, err := conn.Write(frame[:])
	return err
}

func TestTCPClient_ConnectAndSend(t *testing.T) {
	const addr = "127.0.0.1:0"
	ln := fakeCoreServer(t, addr)
	defer ln.Close()

	actualAddr := ln.Addr().String()

	client := NewTCPClient(actualAddr, "api_gateway", "d41d8cd98f00b204e9800998ecf8427e", nil)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	if err := client.Connect(ctx); err != nil {
		t.Fatalf("Connect: %v", err)
	}
	defer client.Close()

	if !client.Healthy() {
		t.Fatal("expected client to be healthy after Connect")
	}

	req := Envelope{
		MsgType:    MsgGatewayCommand,
		SourceRole: RoleGateway,
		SourceID:   "api_gateway",
		TargetRole: RoleServer,
		TargetID:   "SERVER",
		Payload: Payload{
			Command: "ping",
		},
	}

	resp, err := client.Send(ctx, req)
	if err != nil {
		t.Fatalf("Send: %v", err)
	}

	if resp.MsgType != MsgGatewayResponse {
		t.Errorf("expected msg_type %s, got %s", MsgGatewayResponse, resp.MsgType)
	}
}

func TestTCPClient_AuthRejected(t *testing.T) {
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

		readFrameFromConn(conn)
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

	client := NewTCPClient(ln.Addr().String(), "api_gateway", "badpassword", nil)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	err = client.Connect(ctx)
	if err == nil {
		client.Close()
		t.Fatal("expected auth error, got nil")
	}
}

func TestComputeChecksum(t *testing.T) {
	cs1 := computeChecksum(MsgAuthRequest, "api_gateway")
	cs2 := computeChecksum(MsgAuthRequest, "api_gateway")
	if cs1 != cs2 {
		t.Fatalf("checksum not deterministic: %s vs %s", cs1, cs2)
	}
	if len(cs1) == 0 || len(cs1) > 6 {
		t.Fatalf("expected 1-6 char hex checksum, got %q (len %d)", cs1, len(cs1))
	}
	if len(cs1) > 1 && cs1[0] == '0' {
		t.Fatalf("checksum should not be zero-padded, got %q", cs1)
	}
}

func TestTimestamp_RoundTrip(t *testing.T) {
	ts := Now()
	data, err := json.Marshal(ts)
	if err != nil {
		t.Fatal(err)
	}
	var ts2 Timestamp
	if err := json.Unmarshal(data, &ts2); err != nil {
		t.Fatal(err)
	}
	want := ts.UTC().Truncate(time.Millisecond)
	got := ts2.UTC().Truncate(time.Millisecond)
	if !want.Equal(got) {
		t.Fatalf("timestamp roundtrip failed: want %v, got %v", want, got)
	}
}
