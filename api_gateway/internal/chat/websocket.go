package chat

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"strings"
	"time"

	"github.com/gofiber/contrib/websocket"
	"github.com/gofiber/fiber/v2"
	"github.com/golang-jwt/jwt/v5"

	corebridge "lora-chads/api_gateway/internal/core_bridge"
	"lora-chads/api_gateway/internal/shipments"
)

const (
	messageTypeCancelShipment = "cancel_shipment"
	messageTypeCancelResponse = "cancel_response"
	messageTypeEmergencyAlert = "emergency_alert"

	coreEmergencyAlert corebridge.MsgType = "SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT"
	gatewaySourceID                       = "api_gateway"

	clientSendBuffer    = 256
	broadcastBuffer     = 256
	writeWait           = 10 * time.Second
	pongWait            = 40 * time.Second
	pingPeriod          = 30 * time.Second
	maxIncomingMsgBytes = 4 * 1024

	defaultGuestRole = corebridge.RoleCLI
)

type ShipmentCanceller interface {
	CancelShipment(shipmentID string, callerUsername string) (shipments.StatusResponse, error)
}

type WSMessage struct {
	Type       string          `json:"type"`
	Event      string          `json:"event,omitempty"`
	SourceRole string          `json:"source_role,omitempty"`
	SourceID   string          `json:"source_id,omitempty"`
	TargetRole string          `json:"target_role,omitempty"`
	TargetID   string          `json:"target_id,omitempty"`
	Timestamp  string          `json:"timestamp"`
	Payload    json.RawMessage `json:"payload"`
}

type CancelPayload struct {
	ShipmentID string `json:"shipment_id"`
}

type CancelResponsePayload struct {
	ShipmentID string `json:"shipment_id,omitempty"`
	Status     string `json:"status,omitempty"`
	Error      string `json:"error,omitempty"`
}

type Client struct {
	hub      *Hub
	conn     *websocket.Conn
	send     chan []byte
	username string
	role     string
}

type Hub struct {
	secret        string
	logger        *log.Logger
	clients       map[*Client]struct{}
	clientsByName map[string]map[*Client]struct{}
	canceller     ShipmentCanceller
	register      chan *Client
	unregister    chan *Client
	broadcast     chan []byte
}

func NewHub(secret string, logger *log.Logger, canceller ShipmentCanceller) *Hub {
	if logger == nil {
		logger = log.Default()
	}

	return &Hub{
		secret:        secret,
		logger:        logger,
		clients:       make(map[*Client]struct{}),
		clientsByName: make(map[string]map[*Client]struct{}),
		canceller:     canceller,
		register:      make(chan *Client),
		unregister:    make(chan *Client),
		broadcast:     make(chan []byte, broadcastBuffer),
	}
}

func (hub *Hub) Run(ctx context.Context) error {
	for {
		select {
		case <-ctx.Done():
			hub.shutdown()
			return nil
		case client := <-hub.register:
			hub.registerClient(client)
			hub.logger.Printf("chat: client connected user=%s role=%s active=%d", client.username, client.role, len(hub.clients))
		case client := <-hub.unregister:
			hub.unregisterClient(client)
		case message := <-hub.broadcast:
			hub.broadcastMessage(message)
		}
	}
}

func (hub *Hub) HandleUpgrade(ctx *fiber.Ctx) error {
	if !websocket.IsWebSocketUpgrade(ctx) {
		return fiber.ErrUpgradeRequired
	}

	claims, err := hub.parseClaims(strings.TrimSpace(ctx.Query("token")))
	if err != nil {
		return ctx.Status(fiber.StatusUnauthorized).JSON(fiber.Map{"error": err.Error()})
	}

	username := claimString(claims, "sub", "username", "user_id")
	if username == "" {
		username = fmt.Sprintf("guest@%s", ctx.IP())
	}

	role := strings.ToUpper(claimString(claims, "role", "client_type"))
	if role == "" {
		role = defaultGuestRole
	}

	ctx.Locals("user", claims)
	ctx.Locals("username", username)
	ctx.Locals("role", role)

	return ctx.Next()
}

func (hub *Hub) HandleWS(conn *websocket.Conn) {
	client := &Client{
		hub:      hub,
		conn:     conn,
		send:     make(chan []byte, clientSendBuffer),
		username: localString(conn, "username", fmt.Sprintf("guest@%s", conn.IP())),
		role:     localString(conn, "role", defaultGuestRole),
	}

	hub.register <- client
	defer func() {
		hub.unregister <- client
	}()

	go client.writePump()
	client.readPump()
}

func (hub *Hub) Broadcast(data []byte) {
	message := append([]byte(nil), data...)
	select {
	case hub.broadcast <- message:
	default:
		hub.logger.Printf("chat: dropping broadcast, queue full")
	}
}

func (hub *Hub) BroadcastCoreEvent(env corebridge.Envelope) {
	data, err := buildCoreEventMessage(env)
	if err != nil {
		hub.logger.Printf("chat: marshal core event %s: %v", env.MsgType, err)
		return
	}

	hub.Broadcast(data)
}

func (hub *Hub) parseClaims(tokenString string) (jwt.MapClaims, error) {
	if hub.secret == "" {
		return jwt.MapClaims{}, nil
	}

	if tokenString == "" {
		return nil, fmt.Errorf("missing token")
	}

	claims := jwt.MapClaims{}
	token, err := jwt.ParseWithClaims(tokenString, claims, func(token *jwt.Token) (any, error) {
		if _, ok := token.Method.(*jwt.SigningMethodHMAC); !ok {
			return nil, fmt.Errorf("unexpected signing method: %s", token.Method.Alg())
		}

		return []byte(hub.secret), nil
	})
	if err != nil || !token.Valid {
		return nil, fmt.Errorf("invalid token")
	}

	return claims, nil
}

func (hub *Hub) shutdown() {
	for client := range hub.clients {
		hub.removeClient(client)
	}
}

func (hub *Hub) registerClient(client *Client) {
	hub.clients[client] = struct{}{}
	if _, ok := hub.clientsByName[client.username]; !ok {
		hub.clientsByName[client.username] = make(map[*Client]struct{})
	}
	hub.clientsByName[client.username][client] = struct{}{}
}

func (hub *Hub) unregisterClient(client *Client) {
	if !hub.removeClient(client) {
		return
	}
	hub.logger.Printf("chat: client disconnected user=%s active=%d", client.username, len(hub.clients))
}

func (hub *Hub) removeClient(client *Client) bool {
	if _, ok := hub.clients[client]; !ok {
		return false
	}

	delete(hub.clients, client)
	if namedClients, ok := hub.clientsByName[client.username]; ok {
		delete(namedClients, client)
		if len(namedClients) == 0 {
			delete(hub.clientsByName, client.username)
		}
	}
	close(client.send)
	return true
}

func (hub *Hub) broadcastMessage(message []byte) {
	for client := range hub.clients {
		if !client.enqueue(message) {
			hub.logger.Printf("chat: disconnecting slow client user=%s", client.username)
			hub.removeClient(client)
		}
	}
}

func (client *Client) readPump() {
	defer client.conn.Close()

	client.conn.SetReadLimit(maxIncomingMsgBytes)
	client.conn.SetReadDeadline(time.Now().Add(pongWait))
	client.conn.SetPongHandler(func(string) error {
		return client.conn.SetReadDeadline(time.Now().Add(pongWait))
	})

	for {
		_, rawMessage, err := client.conn.ReadMessage()
		if err != nil {
			return
		}

		var message WSMessage
		if err := json.Unmarshal(rawMessage, &message); err != nil {
			client.hub.logger.Printf("chat: invalid client payload from %s: %v", client.username, err)
			continue
		}

		if message.Type != messageTypeCancelShipment {
			client.hub.logger.Printf("chat: unsupported message type %q from %s", message.Type, client.username)
			continue
		}

		if !client.handleCancel(message.Payload) {
			return
		}
	}
}

func (client *Client) handleCancel(rawPayload json.RawMessage) bool {
	var payload CancelPayload
	if err := json.Unmarshal(rawPayload, &payload); err != nil {
		client.hub.logger.Printf("chat: invalid cancel payload from %s: %v", client.username, err)
		return client.replyCancel(shipments.StatusResponse{}, "", fmt.Errorf("invalid cancel payload"))
	}

	payload.ShipmentID = strings.TrimSpace(payload.ShipmentID)
	if payload.ShipmentID == "" {
		return client.replyCancel(shipments.StatusResponse{}, "", fmt.Errorf("shipment_id is required"))
	}
	if client.hub.canceller == nil {
		return client.replyCancel(shipments.StatusResponse{}, payload.ShipmentID, fmt.Errorf("shipment canceller not configured"))
	}

	status, err := client.hub.canceller.CancelShipment(payload.ShipmentID, client.username)
	return client.replyCancel(status, payload.ShipmentID, err)
}

func (client *Client) replyCancel(status shipments.StatusResponse, requestedShipmentID string, cancelErr error) bool {
	data, err := buildCancelResponse(client.username, client.role, requestedShipmentID, status, cancelErr)
	if err != nil {
		client.hub.logger.Printf("chat: marshal cancel response for %s: %v", client.username, err)
		return false
	}
	if client.enqueue(data) {
		return true
	}

	client.hub.logger.Printf("chat: disconnecting slow client user=%s", client.username)
	return false
}

func (client *Client) writePump() {
	ticker := time.NewTicker(pingPeriod)
	defer func() {
		ticker.Stop()
		client.conn.Close()
	}()

	for {
		select {
		case message, ok := <-client.send:
			if err := client.conn.SetWriteDeadline(time.Now().Add(writeWait)); err != nil {
				return
			}
			if !ok {
				_ = client.conn.WriteMessage(websocket.CloseMessage, []byte{})
				return
			}
			if err := client.conn.WriteMessage(websocket.TextMessage, message); err != nil {
				return
			}
		case <-ticker.C:
			if err := client.conn.SetWriteDeadline(time.Now().Add(writeWait)); err != nil {
				return
			}
			if err := client.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				return
			}
		}
	}
}

func buildCoreEventMessage(env corebridge.Envelope) ([]byte, error) {
	if env.MsgType != coreEmergencyAlert {
		return nil, fmt.Errorf("unsupported core event type %s", env.MsgType)
	}

	payload, err := json.Marshal(env.Payload)
	if err != nil {
		return nil, err
	}

	message := WSMessage{
		Type:       messageTypeEmergencyAlert,
		Event:      string(env.MsgType),
		SourceRole: env.SourceRole,
		SourceID:   env.SourceID,
		TargetRole: env.TargetRole,
		TargetID:   env.TargetID,
		Timestamp:  formatTimestamp(env.Timestamp.Time),
		Payload:    payload,
	}

	return json.Marshal(message)
}

func buildCancelResponse(targetID, targetRole, requestedShipmentID string, status shipments.StatusResponse, cancelErr error) ([]byte, error) {
	payload := CancelResponsePayload{
		ShipmentID: requestedShipmentID,
	}
	if status.ShipmentID != "" {
		payload.ShipmentID = status.ShipmentID
	}
	if cancelErr != nil {
		payload.Status = "error"
		payload.Error = cancelErr.Error()
	} else {
		payload.Status = status.Status
		if payload.Status == "" {
			payload.Status = shipments.StatusCancelled
		}
	}

	body, err := json.Marshal(payload)
	if err != nil {
		return nil, err
	}

	message := WSMessage{
		Type:       messageTypeCancelResponse,
		SourceRole: corebridge.RoleGateway,
		SourceID:   gatewaySourceID,
		TargetRole: targetRole,
		TargetID:   targetID,
		Timestamp:  formatTimestamp(time.Time{}),
		Payload:    body,
	}

	return json.Marshal(message)
}

func formatTimestamp(ts time.Time) string {
	if ts.IsZero() {
		ts = time.Now().UTC()
	}

	return ts.UTC().Format(time.RFC3339Nano)
}

func (client *Client) enqueue(message []byte) (sent bool) {
	buffered := append([]byte(nil), message...)
	defer func() {
		if recover() != nil {
			sent = false
		}
	}()

	select {
	case client.send <- buffered:
		return true
	default:
		return false
	}
}

func localString(conn *websocket.Conn, key string, fallback string) string {
	value, ok := conn.Locals(key).(string)
	if !ok || strings.TrimSpace(value) == "" {
		return fallback
	}

	return value
}

func claimString(claims jwt.MapClaims, keys ...string) string {
	for _, key := range keys {
		value, ok := claims[key]
		if !ok {
			continue
		}

		switch typed := value.(type) {
		case string:
			if strings.TrimSpace(typed) != "" {
				return typed
			}
		default:
			stringValue := strings.TrimSpace(fmt.Sprint(typed))
			if stringValue != "" {
				return stringValue
			}
		}
	}

	return ""
}
