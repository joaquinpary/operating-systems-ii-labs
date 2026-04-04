package corebridge

import (
	"fmt"
	"strings"
	"time"
)

const FrameSize = 1024

// Roles — must match C++ #define values in json_manager.h.
const (
	RoleServer    = "SERVER"
	RoleHub       = "HUB"
	RoleWarehouse = "WAREHOUSE"
	RoleCLI       = "CLI"
	RoleGateway   = "GATEWAY"
)

// MsgType identifies the kind of message in the C++ core protocol.
type MsgType string

// GATEWAY↔SERVER message types (parallel to CLI/HUB/WAREHOUSE families).
const (
	MsgAuthRequest     MsgType = "GATEWAY_TO_SERVER__AUTH_REQUEST"
	MsgAuthResponse    MsgType = "SERVER_TO_GATEWAY__AUTH_RESPONSE"
	MsgACK             MsgType = "GATEWAY_TO_SERVER__ACK"
	MsgServerACK       MsgType = "SERVER_TO_GATEWAY__ACK"
	MsgKeepAlive       MsgType = "GATEWAY_TO_SERVER__KEEPALIVE"
	MsgGatewayCommand  MsgType = "GATEWAY_TO_SERVER__COMMAND"
	MsgGatewayResponse MsgType = "SERVER_TO_GATEWAY__COMMAND_RESPONSE"
)

// Timestamp wraps time.Time so that JSON serialisation uses the C++ core's
// millisecond-precision format: "2025-01-15T08:30:45.123Z".
type Timestamp struct{ time.Time }

const timestampLayout = "2006-01-02T15:04:05.000Z"

func (t Timestamp) MarshalJSON() ([]byte, error) {
	return []byte(`"` + t.UTC().Format(timestampLayout) + `"`), nil
}

func (t *Timestamp) UnmarshalJSON(data []byte) error {
	s := strings.Trim(string(data), `"`)
	if s == "" || s == "null" {
		return nil
	}
	parsed, err := time.Parse(timestampLayout, s)
	if err != nil {
		return fmt.Errorf("corebridge: parse timestamp %q: %w", s, err)
	}
	t.Time = parsed
	return nil
}

func Now() Timestamp { return Timestamp{time.Now().UTC()} }

// Envelope is the top-level JSON structure expected by the C++ core.
type Envelope struct {
	MsgType    MsgType   `json:"msg_type"`
	SourceRole string    `json:"source_role"`
	SourceID   string    `json:"source_id"`
	TargetRole string    `json:"target_role"`
	TargetID   string    `json:"target_id"`
	Timestamp  Timestamp `json:"timestamp"`
	Payload    Payload   `json:"payload"`
	Checksum   string    `json:"checksum"`
}

// Payload carries the variable part of the message. Only fields relevant to
// the current msg_type should be populated; empty fields are omitted.
type Payload struct {
	// Auth fields
	Username string `json:"username,omitempty"`
	Password string `json:"password,omitempty"`

	// Response fields
	StatusCode int `json:"status_code,omitempty"`

	// ACK fields
	AckForTimestamp string `json:"ack_for_timestamp,omitempty"`

	// Keepalive / general text
	Message string `json:"message,omitempty"`

	// Gateway command fields
	Command string `json:"command,omitempty"`
	Args    string `json:"args,omitempty"`

	// Inventory / shipment fields
	Items []Item `json:"items,omitempty"`

	// Emergency fields
	EmergencyCode int    `json:"emergency_code,omitempty"`
	EmergencyType string `json:"emergency_type,omitempty"`
	Instructions  string `json:"instructions,omitempty"`

	// Order reference
	OrderTimestamp string `json:"order_timestamp,omitempty"`
}

// Item mirrors the item structure used across inventory and shipment messages.
type Item struct {
	ItemID   int    `json:"item_id"`
	ItemName string `json:"item_name"`
	Quantity int    `json:"quantity"`
}

// computeChecksum calculates the DJB2 hash of (msg_type + source_id),
// masks it to 24 bits, and returns an uppercase hex string matching the C++
// format: snprintf("%lX", hash & 0xFFFFFF) — NO zero-padding.
func computeChecksum(msgType MsgType, sourceID string) string {
	input := string(msgType) + sourceID
	var hash uint32 = 5381
	for i := 0; i < len(input); i++ {
		hash = (hash << 5) + hash + uint32(input[i])
	}
	return fmt.Sprintf("%X", hash&0xFFFFFF)
}
