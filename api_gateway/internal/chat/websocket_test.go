package chat

import (
	"encoding/json"
	"errors"
	"io"
	"log"
	"testing"
	"time"

	corebridge "lora-chads/api_gateway/internal/core_bridge"
	"lora-chads/api_gateway/internal/shipments"
)

type fakeCanceller struct {
	status         shipments.StatusResponse
	err            error
	called         bool
	lastShipmentID string
	lastCaller     string
}

func (fake *fakeCanceller) CancelShipment(shipmentID string, callerUsername string) (shipments.StatusResponse, error) {
	fake.called = true
	fake.lastShipmentID = shipmentID
	fake.lastCaller = callerUsername
	return fake.status, fake.err
}

func TestBuildCoreEventMessageMapsEmergencyAlerts(t *testing.T) {
	timestamp := time.Date(2026, 4, 6, 12, 0, 0, 0, time.UTC)
	env := corebridge.Envelope{
		MsgType:    coreEmergencyAlert,
		SourceRole: corebridge.RoleServer,
		SourceID:   "SERVER",
		TargetRole: corebridge.RoleCLI,
		TargetID:   "ALL",
		Timestamp:  corebridge.Timestamp{Time: timestamp},
		Payload: corebridge.Payload{
			EmergencyCode: 7,
			Instructions:  "Evacuate zone B",
		},
	}

	raw, err := buildCoreEventMessage(env)
	if err != nil {
		t.Fatalf("buildCoreEventMessage() error = %v", err)
	}

	var message WSMessage
	if err := json.Unmarshal(raw, &message); err != nil {
		t.Fatalf("unmarshal message: %v", err)
	}

	if message.Type != messageTypeEmergencyAlert {
		t.Fatalf("Type = %q, want %q", message.Type, messageTypeEmergencyAlert)
	}
	if message.Event != string(coreEmergencyAlert) {
		t.Fatalf("Event = %q, want %q", message.Event, coreEmergencyAlert)
	}
	if message.SourceID != "SERVER" {
		t.Fatalf("SourceID = %q, want SERVER", message.SourceID)
	}
	if message.TargetID != "ALL" {
		t.Fatalf("TargetID = %q, want ALL", message.TargetID)
	}

	var payload corebridge.Payload
	if err := json.Unmarshal(message.Payload, &payload); err != nil {
		t.Fatalf("unmarshal payload: %v", err)
	}
	if payload.EmergencyCode != 7 {
		t.Fatalf("EmergencyCode = %d, want 7", payload.EmergencyCode)
	}
	if payload.Instructions != "Evacuate zone B" {
		t.Fatalf("Instructions = %q, want %q", payload.Instructions, "Evacuate zone B")
	}
}

func TestBuildCoreEventMessageRejectsUnsupportedEvents(t *testing.T) {
	env := corebridge.Envelope{
		MsgType:    corebridge.MsgGatewayResponse,
		SourceRole: corebridge.RoleServer,
		SourceID:   "SERVER",
		Timestamp:  corebridge.Now(),
	}

	if _, err := buildCoreEventMessage(env); err == nil {
		t.Fatal("buildCoreEventMessage() error = nil, want unsupported event error")
	}
}

func TestHubBroadcastsEmergencyAlertsToRegisteredClients(t *testing.T) {
	hub := NewHub("secret", log.New(io.Discard, "", 0), nil)
	client := &Client{
		hub:      hub,
		send:     make(chan []byte, 1),
		username: "citizen-1",
		role:     corebridge.RoleCLI,
	}
	hub.registerClient(client)

	env := corebridge.Envelope{
		MsgType:    coreEmergencyAlert,
		SourceRole: corebridge.RoleServer,
		SourceID:   "SERVER",
		TargetRole: corebridge.RoleCLI,
		TargetID:   "ALL",
		Timestamp:  corebridge.Now(),
		Payload: corebridge.Payload{
			EmergencyCode: 12,
			Instructions:  "Stay inside",
		},
	}
	raw, err := buildCoreEventMessage(env)
	if err != nil {
		t.Fatalf("buildCoreEventMessage() error = %v", err)
	}
	hub.broadcastMessage(raw)

	var message WSMessage
	if err := json.Unmarshal(<-client.send, &message); err != nil {
		t.Fatalf("unmarshal message: %v", err)
	}
	if message.Type != messageTypeEmergencyAlert {
		t.Fatalf("Type = %q, want %q", message.Type, messageTypeEmergencyAlert)
	}
	if message.Event != string(coreEmergencyAlert) {
		t.Fatalf("Event = %q, want %q", message.Event, coreEmergencyAlert)
	}
}

func TestHubTracksClientsByUsername(t *testing.T) {
	hub := NewHub("secret", log.New(io.Discard, "", 0), nil)
	first := &Client{hub: hub, send: make(chan []byte, 1), username: "user-1", role: corebridge.RoleCLI}
	second := &Client{hub: hub, send: make(chan []byte, 1), username: "user-1", role: corebridge.RoleCLI}
	third := &Client{hub: hub, send: make(chan []byte, 1), username: "user-2", role: corebridge.RoleCLI}

	hub.registerClient(first)
	hub.registerClient(second)
	hub.registerClient(third)

	if len(hub.clientsByName["user-1"]) != 2 {
		t.Fatalf("len(clientsByName[user-1]) = %d, want 2", len(hub.clientsByName["user-1"]))
	}
	if len(hub.clientsByName["user-2"]) != 1 {
		t.Fatalf("len(clientsByName[user-2]) = %d, want 1", len(hub.clientsByName["user-2"]))
	}

	hub.unregisterClient(first)
	if len(hub.clientsByName["user-1"]) != 1 {
		t.Fatalf("len(clientsByName[user-1]) after unregister = %d, want 1", len(hub.clientsByName["user-1"]))
	}

	hub.unregisterClient(second)
	if _, ok := hub.clientsByName["user-1"]; ok {
		t.Fatal("clientsByName[user-1] still exists after removing all clients")
	}
}

func TestClientHandleCancelShipmentSuccess(t *testing.T) {
	canceller := &fakeCanceller{
		status: shipments.StatusResponse{
			ShipmentID: "shipment-1",
			Status:     shipments.StatusCancelled,
		},
	}
	hub := NewHub("secret", log.New(io.Discard, "", 0), canceller)
	client := &Client{
		hub:      hub,
		send:     make(chan []byte, 1),
		username: "owner-1",
		role:     corebridge.RoleCLI,
	}

	if ok := client.handleCancel(json.RawMessage(`{"shipment_id":"shipment-1"}`)); !ok {
		t.Fatal("handleCancel() = false, want true")
	}
	if !canceller.called {
		t.Fatal("canceller was not called")
	}
	if canceller.lastShipmentID != "shipment-1" {
		t.Fatalf("lastShipmentID = %q, want shipment-1", canceller.lastShipmentID)
	}
	if canceller.lastCaller != "owner-1" {
		t.Fatalf("lastCaller = %q, want owner-1", canceller.lastCaller)
	}

	var message WSMessage
	if err := json.Unmarshal(<-client.send, &message); err != nil {
		t.Fatalf("unmarshal message: %v", err)
	}
	if message.Type != messageTypeCancelResponse {
		t.Fatalf("Type = %q, want %q", message.Type, messageTypeCancelResponse)
	}

	var payload CancelResponsePayload
	if err := json.Unmarshal(message.Payload, &payload); err != nil {
		t.Fatalf("unmarshal payload: %v", err)
	}
	if payload.Status != shipments.StatusCancelled {
		t.Fatalf("Status = %q, want %q", payload.Status, shipments.StatusCancelled)
	}
	if payload.Error != "" {
		t.Fatalf("Error = %q, want empty", payload.Error)
	}
}

func TestClientHandleCancelShipmentError(t *testing.T) {
	canceller := &fakeCanceller{
		err: errors.New("shipment does not belong to current user"),
	}
	hub := NewHub("secret", log.New(io.Discard, "", 0), canceller)
	client := &Client{
		hub:      hub,
		send:     make(chan []byte, 1),
		username: "owner-2",
		role:     corebridge.RoleCLI,
	}

	if ok := client.handleCancel(json.RawMessage(`{"shipment_id":"shipment-2"}`)); !ok {
		t.Fatal("handleCancel() = false, want true")
	}

	var message WSMessage
	if err := json.Unmarshal(<-client.send, &message); err != nil {
		t.Fatalf("unmarshal message: %v", err)
	}

	var payload CancelResponsePayload
	if err := json.Unmarshal(message.Payload, &payload); err != nil {
		t.Fatalf("unmarshal payload: %v", err)
	}
	if payload.Status != "error" {
		t.Fatalf("Status = %q, want error", payload.Status)
	}
	if payload.Error == "" {
		t.Fatal("Error = empty, want error message")
	}
}
