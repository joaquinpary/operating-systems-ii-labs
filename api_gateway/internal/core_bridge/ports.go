package core_bridge

import "context"

// Bridge is the primary port through which the rest of the application
// communicates with the C++ logistics core. Implementations must handle
// the 1024-byte framing, authentication handshake, and checksum protocol.
type Bridge interface {
	// Connect establishes the TCP connection and performs the authentication
	// handshake with the C++ core. It must be called before Send.
	Connect(ctx context.Context) error

	// Send transmits an Envelope to the C++ core and blocks until a response
	// Envelope is received. The implementation handles framing and checksums.
	Send(ctx context.Context, msg Envelope) (Envelope, error)

	// Close gracefully shuts down the TCP connection.
	Close() error

	// Healthy reports whether the underlying TCP connection is still alive.
	Healthy() bool
}
