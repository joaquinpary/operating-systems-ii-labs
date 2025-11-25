#ifndef JSON_MANAGER_H
#define JSON_MANAGER_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "cJSON.h"
#include <time.h>
// #include <zlib.h>

#define HUB_TO_SERVER__AUTH_REQUEST "HUB_TO_SERVER__AUTH_REQUEST"
#define WAREHOUSE_TO_SERVER__AUTH_REQUEST "WAREHOUSE_TO_SERVER__AUTH_REQUEST"
#define SERVER_TO_HUB__AUTH_RESPONSE "SERVER_TO_HUB__AUTH_RESPONSE"
#define SERVER_TO_WAREHOUSE__AUTH_RESPONSE "SERVER_TO_WAREHOUSE__AUTH_RESPONSE"
#define HUB_TO_SERVER__KEEPALIVE "HUB_TO_SERVER__KEEPALIVE"
#define WAREHOUSE_TO_SERVER__KEEPALIVE "WAREHOUSE_TO_SERVER__KEEPALIVE"
#define HUB_TO_SERVER__INVENTORY_UPDATE "HUB_TO_SERVER__INVENTORY_UPDATE"
#define WAREHOUSE_TO_SERVER__INVENTORY_UPDATE "WAREHOUSE_TO_SERVER__INVENTORY_UPDATE"
#define HUB_TO_SERVER__EMERGENCY_ALERT "HUB_TO_SERVER__EMERGENCY_ALERT"
#define WAREHOUSE_TO_SERVER__EMERGENCY_ALERT "WAREHOUSE_TO_SERVER__EMERGENCY_ALERT"
#define HUB_TO_SERVER__STOCK_REQUEST "HUB_TO_SERVER__STOCK_REQUEST"
#define HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION "HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION"
#define WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE "WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE"
#define WAREHOUSE_TO_SERVER__REPLENISH_REQUEST "WAREHOUSE_TO_SERVER__REPLENISH_REQUEST"
#define SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB "SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB"
#define SERVER_TO_WAREHOUSE__RESTOCK_NOTICE "SERVER_TO_WAREHOUSE__RESTOCK_NOTICE"
#define SERVER_TO_HUB__INCOMING_STOCK_NOTICE "SERVER_TO_HUB__INCOMING_STOCK_NOTICE"
#define SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT "SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT"
#define HUB_TO_SERVER__ACK "HUB_TO_SERVER__ACK"
#define WAREHOUSE_TO_SERVER__ACK "WAREHOUSE_TO_SERVER__ACK"
#define SERVER_TO_HUB__ACK "SERVER_TO_HUB__ACK"
#define SERVER_TO_WAREHOUSE__ACK "SERVER_TO_WAREHOUSE__ACK"

#define WAREHOUSE "WAREHOUSE"
#define HUB "HUB"
#define SERVER "SERVER"
#define CLI "CLI"

#define MIN_SIZE 8
#define MESSAGE_TYPE_SIZE 64
#define SOURCE_ROLE_SIZE 16
#define SOURCE_ID_SIZE 16
#define TARGET_ID_SIZE 16
#define TIMESTAMP_SIZE 32
#define CHECKSUM_SIZE 8
#define CREDENTIALS_SIZE 64

#define ITEM_NAME_SIZE 32
#define QUANTITY_ITEMS 6

#define MAX_BUFFER_SIZE 1024

typedef struct inventory_item
{
    int item_id;
    char item_name[ITEM_NAME_SIZE];
    int quantity;
} inventory_item_t;

typedef struct payload_items_list
{
    inventory_item_t items[QUANTITY_ITEMS];
} payload_items_list;

typedef struct payload_status
{
    int status_code;
} payload_status;

typedef payload_items_list payload_inventory_update;
typedef payload_items_list payload_stock_request;
typedef payload_items_list payload_receipt_confirmation;
typedef payload_items_list payload_shipment_notice;
typedef payload_items_list payload_order_stock;
typedef payload_items_list payload_restock_notice;

typedef payload_status payload_auth_response;
typedef payload_status payload_acknowledgment;

typedef struct payload_auth_request
{
    char username[CREDENTIALS_SIZE];
    char password[CREDENTIALS_SIZE];
} payload_auth_request;

typedef struct payload_keepalive
{
    char message;
} payload_keepalive;

typedef struct payload_client_emergency_alert
{
    int emergency_code;
    char emergency_type[20];
} payload_client_emergency_alert;

typedef struct payload_server_emergency_alert
{
    int emergency_code;
    char instructions[100];
} payload_server_emergency_alert;

typedef union payload_t{
    payload_auth_request client_auth_request;
    payload_auth_response server_auth_response;
    payload_keepalive keepalive;
    payload_inventory_update inventory_update;
    payload_client_emergency_alert client_emergency;
    payload_server_emergency_alert server_emergency;
    payload_stock_request stock_request;
    payload_receipt_confirmation receipt_confirmation;
    payload_shipment_notice shipment_notice;
    payload_order_stock order_stock;
    payload_restock_notice restock_notice;
    payload_acknowledgment acknowledgment;
} payload_t;

typedef struct message_t
{
    char msg_type[MESSAGE_TYPE_SIZE];
    char source_role[SOURCE_ROLE_SIZE];
    char source_id[SOURCE_ID_SIZE];
    char target_id[TARGET_ID_SIZE];
    char timestamp[TIMESTAMP_SIZE];
    payload_t payload;
    char checksum[CHECKSUM_SIZE];

} message_t;

int serialize_message_to_json(const message_t *msg, char *out);

int deserialize_message_from_json(const char *json, message_t *out);

#ifdef __cplusplus
}
#endif

#endif