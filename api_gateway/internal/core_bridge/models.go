package core_bridge

import "encoding/json"

const BufferSize = 1024

type Role string

const (
	RoleHub       Role = "HUB"
	RoleWarehouse Role = "WAREHOUSE"
	RoleServer    Role = "SERVER"
	RoleCLI       Role = "CLI"
)

type MessageType string

const (
	HubToServerAuthRequest              MessageType = "HUB_TO_SERVER__AUTH_REQUEST"
	HubToServerKeepalive                MessageType = "HUB_TO_SERVER__KEEPALIVE"
	HubToServerInventoryUpdate          MessageType = "HUB_TO_SERVER__INVENTORY_UPDATE"
	HubToServerStockRequest             MessageType = "HUB_TO_SERVER__STOCK_REQUEST"
	HubToServerStockReceiptConfirmation MessageType = "HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION"
	HubToServerEmergencyAlert           MessageType = "HUB_TO_SERVER__EMERGENCY_ALERT"
	HubToServerACK                      MessageType = "HUB_TO_SERVER__ACK"

	ServerToHubAuthResponse        MessageType = "SERVER_TO_HUB__AUTH_RESPONSE"
	ServerToHubInventoryUpdate     MessageType = "SERVER_TO_HUB__INVENTORY_UPDATE"
	ServerToHubIncomingStockNotice MessageType = "SERVER_TO_HUB__INCOMING_STOCK_NOTICE"
	ServerToHubACK                 MessageType = "SERVER_TO_HUB__ACK"

	WarehouseToServerAuthRequest              MessageType = "WAREHOUSE_TO_SERVER__AUTH_REQUEST"
	WarehouseToServerKeepalive                MessageType = "WAREHOUSE_TO_SERVER__KEEPALIVE"
	WarehouseToServerInventoryUpdate          MessageType = "WAREHOUSE_TO_SERVER__INVENTORY_UPDATE"
	WarehouseToServerShipmentNotice           MessageType = "WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE"
	WarehouseToServerReplenishRequest         MessageType = "WAREHOUSE_TO_SERVER__REPLENISH_REQUEST"
	WarehouseToServerStockReceiptConfirmation MessageType = "WAREHOUSE_TO_SERVER__STOCK_RECEIPT_CONFIRMATION"
	WarehouseToServerEmergencyAlert           MessageType = "WAREHOUSE_TO_SERVER__EMERGENCY_ALERT"
	WarehouseToServerACK                      MessageType = "WAREHOUSE_TO_SERVER__ACK"

	ServerToWarehouseAuthResponse         MessageType = "SERVER_TO_WAREHOUSE__AUTH_RESPONSE"
	ServerToWarehouseInventoryUpdate      MessageType = "SERVER_TO_WAREHOUSE__INVENTORY_UPDATE"
	ServerToWarehouseOrderToDispatchStock MessageType = "SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB"
	ServerToWarehouseRestockNotice        MessageType = "SERVER_TO_WAREHOUSE__RESTOCK_NOTICE"
	ServerToWarehouseACK                  MessageType = "SERVER_TO_WAREHOUSE__ACK"

	CLIToServerAuthRequest    MessageType = "CLI_TO_SERVER__AUTH_REQUEST"
	CLIToServerAdminCommand   MessageType = "CLI_TO_SERVER__ADMIN_COMMAND"
	ServerToCLIAuthResponse   MessageType = "SERVER_TO_CLI__AUTH_RESPONSE"
	ServerToAllEmergencyAlert MessageType = "SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT"
)

type Message struct {
	MsgType    MessageType     `json:"msg_type"`
	SourceRole Role            `json:"source_role"`
	SourceID   string          `json:"source_id"`
	TargetRole Role            `json:"target_role"`
	TargetID   string          `json:"target_id"`
	Timestamp  string          `json:"timestamp"`
	Payload    json.RawMessage `json:"payload"`
	Checksum   string          `json:"checksum"`
}

type CredentialsPayload struct {
	Username string `json:"username"`
	Password string `json:"password"`
}

type StatusPayload struct {
	StatusCode int `json:"status_code"`
}

type Item struct {
	ItemID   int    `json:"item_id"`
	ItemName string `json:"item_name"`
	Quantity int    `json:"quantity"`
}

type InventoryPayload struct {
	Items          []Item `json:"items"`
	OrderTimestamp string `json:"order_timestamp,omitempty"`
}

type AckPayload struct {
	StatusCode      int    `json:"status_code"`
	AckForTimestamp string `json:"ack_for_timestamp"`
}

type KeepalivePayload struct {
	Message string `json:"message"`
}

type EmergencyPayload struct {
	EmergencyCode int    `json:"emergency_code"`
	EmergencyType string `json:"emergency_type,omitempty"`
	Instructions  string `json:"instructions,omitempty"`
}
