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
#define HUB_TO_SERVER__KEEPALIVE "HUB_TO_SERVER__KEEPALIVE"
#define HUB_TO_SERVER__INVENTORY_UPDATE "HUB_TO_SERVER__INVENTORY_UPDATE"
#define HUB_TO_SERVER__EMERGENCY_ALERT "HUB_TO_SERVER__EMERGENCY_ALERT"
#define HUB_TO_SERVER__STOCK_REQUEST "HUB_TO_SERVER__STOCK_REQUEST"
#define HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION "HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION"
#define HUB_TO_SERVER__ACK "HUB_TO_SERVER__ACK"
#define WAREHOUSE_TO_SERVER__AUTH_REQUEST "WAREHOUSE_TO_SERVER__AUTH_REQUEST"
#define WAREHOUSE_TO_SERVER__KEEPALIVE "WAREHOUSE_TO_SERVER__KEEPALIVE"
#define WAREHOUSE_TO_SERVER__INVENTORY_UPDATE "WAREHOUSE_TO_SERVER__INVENTORY_UPDATE"
#define WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE "WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE"
#define WAREHOUSE_TO_SERVER__REPLENISH_REQUEST "WAREHOUSE_TO_SERVER__REPLENISH_REQUEST"
#define WAREHOUSE_TO_SERVER__STOCK_RECEIPT_CONFIRMATION "WAREHOUSE_TO_SERVER__STOCK_RECEIPT_CONFIRMATION"
#define WAREHOUSE_TO_SERVER__EMERGENCY_ALERT "WAREHOUSE_TO_SERVER__EMERGENCY_ALERT"
#define WAREHOUSE_TO_SERVER__ACK "WAREHOUSE_TO_SERVER__ACK"
#define SERVER_TO_HUB__AUTH_RESPONSE "SERVER_TO_HUB__AUTH_RESPONSE"
#define SERVER_TO_WAREHOUSE__AUTH_RESPONSE "SERVER_TO_WAREHOUSE__AUTH_RESPONSE"
#define SERVER_TO_HUB__INVENTORY_UPDATE "SERVER_TO_HUB__INVENTORY_UPDATE"
#define SERVER_TO_WAREHOUSE__INVENTORY_UPDATE "SERVER_TO_WAREHOUSE__INVENTORY_UPDATE"
#define SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB "SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB"
#define SERVER_TO_WAREHOUSE__RESTOCK_NOTICE "SERVER_TO_WAREHOUSE__RESTOCK_NOTICE"
#define SERVER_TO_HUB__INCOMING_STOCK_NOTICE "SERVER_TO_HUB__INCOMING_STOCK_NOTICE"
#define SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT "SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT"
#define SERVER_TO_HUB__ACK "SERVER_TO_HUB__ACK"
#define SERVER_TO_WAREHOUSE__ACK "SERVER_TO_WAREHOUSE__ACK"
#define CLI_TO_SERVER__AUTH_REQUEST "CLI_TO_SERVER__AUTH_REQUEST"
#define CLI_TO_SERVER__ADMIN_COMMAND "CLI_TO_SERVER__ADMIN_COMMAND"
#define SERVER_TO_CLI__AUTH_RESPONSE "SERVER_TO_CLI__AUTH_RESPONSE"

#define WAREHOUSE "WAREHOUSE"
#define HUB "HUB"
#define SERVER "SERVER"
#define CLI "CLI"

// Message type prefixes for parsing (used in create_items_message)
#define MSG_PREFIX_HUB_TO_SERVER "HUB_TO_SERVER"
#define MSG_PREFIX_WAREHOUSE_TO_SERVER "WAREHOUSE_TO_SERVER"
#define MSG_PREFIX_SERVER_TO_HUB "SERVER_TO_HUB"
#define MSG_PREFIX_SERVER_TO_WAREHOUSE "SERVER_TO_WAREHOUSE"

#define AUTH_RESPONSE "AUTH_RESPONSE"
#define ACK "ACK"
#define STOCK_REQUEST "STOCK_REQUEST"
#define INVENTORY_UPDATE "INVENTORY_UPDATE"
#define RECEIPT_CONFIRMATION "RECEIPT_CONFIRMATION"
#define SHIPMENT_NOTICE "SHIPMENT_NOTICE"
#define REPLENISH_REQUEST "REPLENISH_REQUEST"
#define ORDER_DISPATCH "ORDER_DISPATCH"
#define RESTOCK_NOTICE "RESTOCK_NOTICE"
#define INCOMING_STOCK_NOTICE "INCOMING_STOCK_NOTICE"
#define KEEPALIVE "KEEPALIVE"
#define ALIVE "ALIVE"

#define MIN_SIZE 8
#define MESSAGE_TYPE_SIZE 64
#define ROLE_SIZE 16
#define ID_SIZE 16
#define TIMESTAMP_SIZE 32
#define CHECKSUM_SIZE 8
#define CREDENTIALS_SIZE 64
#define DESCRIPTION_SIZE 128

#define ITEM_NAME_SIZE 32
#define QUANTITY_ITEMS 6

#define EMERGENCY_TYPE_SIZE 20
#define EMERGENCY_INSTRUCTIONS_SIZE 100

#define OK 200

#define BUFFER_SIZE 1024

    typedef struct inventory_item
    {
        int item_id;
        char item_name[ITEM_NAME_SIZE];
        int quantity;
    } inventory_item_t;

    typedef struct payload_items_list
    {
        inventory_item_t items[QUANTITY_ITEMS];
        char order_timestamp[TIMESTAMP_SIZE];
    } payload_items_list;

    typedef payload_items_list payload_inventory_update;     // HUB - WAREHOUSE - SERVER
    typedef payload_items_list payload_stock_request;        // HUB
    typedef payload_items_list payload_receipt_confirmation; // HUB - WAREHOUSE -> SERVER
    typedef payload_items_list payload_shipment_notice;      // WAREHOUSE -> SERVER
    typedef payload_items_list payload_order_stock;          // SERVER -> WAREHOUSE
    typedef payload_items_list payload_restock_notice;       // SERVER -> HUB - WAREHOUSE

    typedef struct payload_auth_response
    {
        int status_code;
    } payload_auth_response;

    typedef struct payload_acknowledgment
    {
        int status_code;
        char ack_for_timestamp[TIMESTAMP_SIZE];
    } payload_acknowledgment;

    typedef struct payload_auth_request
    {
        char username[CREDENTIALS_SIZE];
        char password[CREDENTIALS_SIZE];
    } payload_auth_request;

    typedef struct payload_keepalive
    {
        char message[DESCRIPTION_SIZE];
    } payload_keepalive;

    typedef struct payload_client_emergency_alert
    {
        int emergency_code;
        char emergency_type[EMERGENCY_TYPE_SIZE];
    } payload_client_emergency_alert;

    typedef struct payload_server_emergency_alert
    {
        int emergency_code;
        char instructions[EMERGENCY_INSTRUCTIONS_SIZE];
    } payload_server_emergency_alert;

    typedef union payload_t {
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
        char source_role[ROLE_SIZE];
        char source_id[ID_SIZE];
        char target_role[ROLE_SIZE];
        char target_id[ID_SIZE];
        char timestamp[TIMESTAMP_SIZE];
        payload_t payload;
        char checksum[CHECKSUM_SIZE];

    } message_t;

    /*@brief
     * Serializes a message_t structure into a JSON string.
     * @param msg Pointer to the message_t structure to serialize.
     * @param out Buffer to store the resulting JSON string.
     * @return 0 on success, negative value on error.
     */
    int serialize_message_to_json(const message_t* msg, char* out);

    /*@brief
     * Deserializes a JSON string into a message_t structure.
     * @param json The JSON string to deserialize.
     * @param out Pointer to the message_t structure to populate.
     * @return 0 on success, negative value on error.
     */
    int deserialize_message_from_json(const char* json, message_t* out);

    /*@brief
     * Creates an authentication request message.
     * @param out Pointer to the message_t structure to populate.
     * @param source_role Source role (HUB or WAREHOUSE).
     * @param source_id Source identifier (e.g., "client_0001").
     * @param username Username for authentication.
     * @param password Password for authentication.
     * @return 0 on success, negative value on error.
     */
    int create_auth_request_message(message_t* out, const char* source_role, const char* source_id,
                                    const char* username, const char* password);

    /*@brief
     * Creates a keepalive message.
     * @param out Pointer to the message_t structure to populate.
     * @param source_role Source role (HUB or WAREHOUSE).
     * @param source_id Source identifier (e.g., "client_0001").
     * @param message Keepalive message content.
     * @return 0 on success, negative value on error.
     */
    int create_keepalive_message(message_t* out, const char* source_role, const char* source_id, const char* message);

    /*@brief
     * Generic function to create any message with items payload.
     * Uses the exact msg_type string provided (e.g., HUB_TO_SERVER__INVENTORY_UPDATE).
     * This ensures consistency between message creation and reception - you use the same
     * string constant in both client and server code.
     *
     * @param out Pointer to the message_t structure to populate.
     * @param msg_type Complete message type (use the #define constants like HUB_TO_SERVER__INVENTORY_UPDATE).
     * @param source_id Source identifier (e.g., "client_0001").
     * @param target_id Target identifier (e.g., "SERVER" or specific client ID).
     * @param items Array of inventory items (max QUANTITY_ITEMS).
     * @param item_count Number of items in the array.
     * @param order_timestamp Optional timestamp for receipt confirmation messages (NULL for other message types).
     * @return 0 on success, negative value on error.
     *
     * Example usage:
     *   create_items_message(&msg, HUB_TO_SERVER__STOCK_REQUEST, "client_0001", "SERVER", items, count, NULL);
     *   create_items_message(&msg, HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION, "client_0001", "SERVER", items, count,
     * original_timestamp);
     */
    int create_items_message(message_t* out, const char* msg_type, const char* source_id, const char* target_id,
                             const inventory_item_t* items, int item_count, const char* order_timestamp);

    /*@brief
     * Creates an authentication response message (SERVER to client).
     * @param out Pointer to the message_t structure to populate.
     * @param target_role Target role (HUB or WAREHOUSE).
     * @param target_id Target identifier (client ID).
     * @param status_code Status code (e.g., 200 for success, 401 for unauthorized).
     * @return 0 on success, negative value on error.
     */
    int create_auth_response_message(message_t* out, const char* target_role, const char* target_id, int status_code);

    /*@brief
     * Creates an acknowledgement (ACK) message.
     * @param out Pointer to the message_t structure to populate.
     * @param source_role Source role (HUB, WAREHOUSE, or SERVER).
     * @param source_id Source identifier.
     * @param target_role Target role (HUB, WAREHOUSE, or SERVER).
     * @param target_id Target identifier.
     * @param ack_for_timestamp The timestamp of the message being acknowledged.
     * @param status_code Status code (typically 200 for success).
     * @return 0 on success, negative value on error.
     */
    int create_acknowledgment_message(message_t* out, const char* source_role, const char* source_id,
                                      const char* target_role, const char* target_id, const char* ack_for_timestamp,
                                      int status_code);

    /*@brief
     * Creates an emergency alert message from client (HUB or WAREHOUSE).
     * @param out Pointer to the message_t structure to populate.
     * @param source_role Source role (HUB or WAREHOUSE).
     * @param source_id Source identifier (e.g., "client_0001").
     * @param emergency_code Emergency code number.
     * @param emergency_type Type/description of emergency.
     * @return 0 on success, negative value on error.
     */
    int create_client_emergency_message(message_t* out, const char* source_role, const char* source_id,
                                        int emergency_code, const char* emergency_type);

    /*@brief
     * Creates a server emergency alert message (SERVER to all clients).
     * @param out Pointer to the message_t structure to populate.
     * @param emergency_code Emergency code number.
     * @param instructions Instructions for clients.
     * @return 0 on success, negative value on error.
     */
    int create_server_emergency_message(message_t* out, int emergency_code, const char* instructions);

#ifdef __cplusplus
}
#endif

#endif
