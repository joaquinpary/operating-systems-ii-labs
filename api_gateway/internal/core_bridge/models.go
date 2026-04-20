package core_bridge

import (
	"encoding/json"
	"fmt"
	"strings"
	"time"
)

const FrameSize = 1024

const (
	RoleServer    = "SERVER"
	RoleHub       = "HUB"
	RoleWarehouse = "WAREHOUSE"
	RoleCLI       = "CLI"
	RoleGateway   = "GATEWAY"
)

type MsgType string

const (
	MsgAuthRequest     MsgType = "GATEWAY_TO_SERVER__AUTH_REQUEST"
	MsgAuthResponse    MsgType = "SERVER_TO_GATEWAY__AUTH_RESPONSE"
	MsgACK             MsgType = "GATEWAY_TO_SERVER__ACK"
	MsgServerACK       MsgType = "SERVER_TO_GATEWAY__ACK"
	MsgKeepAlive       MsgType = "GATEWAY_TO_SERVER__KEEPALIVE"
	MsgGatewayCommand  MsgType = "GATEWAY_TO_SERVER__COMMAND"
	MsgGatewayResponse MsgType = "SERVER_TO_GATEWAY__COMMAND_RESPONSE"
)

type Timestamp struct{ time.Time }

const timestampLayout = "2006-01-02T15:04:05.000Z"

// MarshalJSON encodes the timestamp using the core protocol format.
func (t Timestamp) MarshalJSON() ([]byte, error) {
	return []byte(`"` + t.UTC().Format(timestampLayout) + `"`), nil
}

// UnmarshalJSON decodes the core protocol timestamp format into time.Time.
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

// Now returns the current UTC time using the core protocol wrapper.
func Now() Timestamp { return Timestamp{time.Now().UTC()} }

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

type Payload struct {
	Username string `json:"username,omitempty"`
	Password string `json:"password,omitempty"`

	StatusCode int `json:"status_code,omitempty"`

	AckForTimestamp string `json:"ack_for_timestamp,omitempty"`

	Message string `json:"message,omitempty"`

	Command string `json:"command,omitempty"`
	Args    string `json:"args,omitempty"`
	Status  string `json:"status,omitempty"`

	ShipmentID    string `json:"shipment_id,omitempty"`
	TransactionID string `json:"transaction_id,omitempty"`
	Items         []Item `json:"items,omitempty"`

	EmergencyCode int    `json:"emergency_code,omitempty"`
	EmergencyType string `json:"emergency_type,omitempty"`
	Instructions  string `json:"instructions,omitempty"`

	OrderTimestamp string `json:"order_timestamp,omitempty"`

	Data json.RawMessage `json:"data,omitempty"`
}

type Item struct {
	ItemID   int    `json:"item_id"`
	ItemName string `json:"item_name"`
	Quantity int    `json:"quantity"`
}

type Message struct {
	MsgType    MsgType         `json:"msg_type"`
	SourceRole string          `json:"source_role"`
	SourceID   string          `json:"source_id"`
	TargetRole string          `json:"target_role"`
	TargetID   string          `json:"target_id"`
	Timestamp  string          `json:"timestamp"`
	Payload    json.RawMessage `json:"payload"`
	Checksum   string          `json:"checksum"`
}

// computeChecksum calculates the DJB2 hash of (msg_type + source_id),
// masks it to 24 bits, and returns an uppercase hex string matching the C++
// format: snprintf("%lX", hash & 0xFFFFFF) with no zero-padding.
func computeChecksum(msgType MsgType, sourceID string) string {
	input := string(msgType) + sourceID
	var hash uint32 = 5381
	for i := 0; i < len(input); i++ {
		hash = (hash << 5) + hash + uint32(input[i])
	}
	return fmt.Sprintf("%X", hash&0xFFFFFF)
}
