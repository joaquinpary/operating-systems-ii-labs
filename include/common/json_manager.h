#ifndef JSON_MANAGER_H
#define JSON_MANAGER_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "cJSON.h"
#include <time.h>
#include <zlib.h>

#define BUFFER_SIZE 256
#define MIN_SIZE 32
#define TIMESTAMP_SIZE 32
#define CHECKSUM_SIZE 9
#define USER_PASS_SIZE 17
#define SESSION_TOKEN_SIZE 37
#define ITEM_TYPE 6

    // cambiar username por client_id
    typedef struct
    {
        char host[BUFFER_SIZE];
        char port[MIN_SIZE];
        char protocol[MIN_SIZE];
        char ip_version[MIN_SIZE];
    } connection_parameters;

    typedef struct
    {
        char client_type[MIN_SIZE];
        char client_id[MIN_SIZE];
        char username[USER_PASS_SIZE];
        char password[USER_PASS_SIZE];
        connection_parameters connection_params;
    } init_params_client;

    // PAYLOADS
    typedef struct
    {
        char client_id[MIN_SIZE];
        char type[MIN_SIZE];
        char username[USER_PASS_SIZE];
        char password[USER_PASS_SIZE];
        char timestamp[TIMESTAMP_SIZE];
    } payload_client_auth_request;

    typedef struct
    {
        char status[MIN_SIZE];
        char session_token[SESSION_TOKEN_SIZE];
        char message[BUFFER_SIZE];
        char timestamp[TIMESTAMP_SIZE];
    } payload_server_auth_response;

    typedef struct
    {
        char username[USER_PASS_SIZE];
        char session_token[SESSION_TOKEN_SIZE];
        char timestamp[TIMESTAMP_SIZE];
    } payload_client_keepalive;

    typedef struct
    {
        char item[MIN_SIZE];
        unsigned int quantity;
    } inventory_item;

    typedef struct
    {
        char username[USER_PASS_SIZE];
        char session_token[SESSION_TOKEN_SIZE];
        inventory_item items[ITEM_TYPE];
        char timestamp[TIMESTAMP_SIZE];
    } payload_client_inventory_update;

    typedef struct
    {
        char alert_type[MIN_SIZE];
        char timestamp[TIMESTAMP_SIZE];
    } payload_server_emergency_alert;

    typedef struct
    {
        char username[USER_PASS_SIZE];
        char session_token[SESSION_TOKEN_SIZE];
        char status[MIN_SIZE];
        char timestamp[TIMESTAMP_SIZE];
    } payload_client_acknowledgment;

    typedef struct
    {
        char username[USER_PASS_SIZE];
        char session_token[SESSION_TOKEN_SIZE];
        char emergency_type[MIN_SIZE];
        char timestamp[TIMESTAMP_SIZE];
    } payload_client_emergency_alert;

    typedef struct
    {
        // char client_id[MIN_SIZE];
        inventory_item items[ITEM_TYPE];
        char timestamp[TIMESTAMP_SIZE];
    } payload_server_w_stock_hub;

    typedef struct
    {
        char username[USER_PASS_SIZE];
        char session_token[SESSION_TOKEN_SIZE];
        inventory_item items[ITEM_TYPE];
        char timestamp[TIMESTAMP_SIZE];
    } payload_warehouse_send_stock_to_hub;

    typedef struct
    {
        char username[USER_PASS_SIZE];
        char session_token[SESSION_TOKEN_SIZE];
        inventory_item items[ITEM_TYPE];
        char timestamp[TIMESTAMP_SIZE];
    } payload_warehouse_request_stock;

    typedef struct
    {
        inventory_item items[ITEM_TYPE];
        char timestamp[TIMESTAMP_SIZE];
    } payload_server_w_stock_warehouse;

    typedef struct
    {
        // to be implemented
    } payload_server_h_request_delivery;

    typedef struct
    {
        char username[USER_PASS_SIZE];
        char session_token[SESSION_TOKEN_SIZE];
        inventory_item items[ITEM_TYPE];
        char timestamp[TIMESTAMP_SIZE];
    } payload_hub_request_stock;

    typedef struct
    {
        inventory_item items[ITEM_TYPE];
        char timestamp[TIMESTAMP_SIZE];
    } payload_server_h_send_stock;

    // MESSAGE STRUCTURES

    typedef struct
    {
        char type[MIN_SIZE];
        payload_client_auth_request payload;
        char checksum[CHECKSUM_SIZE];
    } client_auth_request;

    typedef struct
    {
        char type[MIN_SIZE];
        payload_server_auth_response payload;
        char checksum[CHECKSUM_SIZE];
    } server_auth_response;

    typedef struct
    {
        char type[MIN_SIZE];
        payload_client_keepalive payload;
        char checksum[BUFFER_SIZE];
    } client_keepalive;

    typedef struct
    {
        char type[MIN_SIZE];
        payload_client_inventory_update payload;
        char checksum[BUFFER_SIZE];
    } client_inventory_update;

    typedef struct
    {
        char type[MIN_SIZE];
        payload_server_emergency_alert payload;
        char checksum[BUFFER_SIZE];
    } server_emergency_alert;

    typedef struct
    {
        char type[MIN_SIZE];
        payload_client_acknowledgment payload;
        char checksum[BUFFER_SIZE];
    } client_acknowledgment;

    typedef struct
    {
        char type[MIN_SIZE];
        payload_client_emergency_alert payload;
        char checksum[BUFFER_SIZE];
    } client_emergency_alert;

    typedef struct
    {
        char type[MIN_SIZE];
        payload_server_w_stock_hub payload;
        char checksum[BUFFER_SIZE];
    } server_w_stock_hub;

    typedef struct
    {
        char type[MIN_SIZE];
        payload_warehouse_send_stock_to_hub payload;
        char checksum[BUFFER_SIZE];
    } warehouse_send_stock_to_hub;

    typedef struct
    {
        char type[MIN_SIZE];
        payload_warehouse_request_stock payload;
        char checksum[BUFFER_SIZE];
    } warehouse_request_stock;

    typedef struct
    {
        char type[MIN_SIZE];
        payload_server_w_stock_warehouse payload;
        char checksum[BUFFER_SIZE];
    } server_w_stock_warehouse;

    typedef struct
    {
        char type[MIN_SIZE];
        payload_server_h_request_delivery payload;
        char checksum[BUFFER_SIZE];
    } server_h_request_delivery;

    typedef struct
    {
        char type[MIN_SIZE];
        payload_hub_request_stock payload;
        char checksum[BUFFER_SIZE];
    } hub_request_stock;

    typedef struct
    {
        char type[MIN_SIZE];
        payload_server_h_send_stock payload;
        char checksum[BUFFER_SIZE];
    } server_h_send_stock;

    typedef struct
    {
        char type[MIN_SIZE];
        char username[USER_PASS_SIZE];
        char session_token[SESSION_TOKEN_SIZE];
        char message_type[MIN_SIZE];
        char timestamp[TIMESTAMP_SIZE];
        char checksum[CHECKSUM_SIZE];
    } cli_message;

    typedef struct
    {
        char type[MIN_SIZE];
        char timestamp[TIMESTAMP_SIZE];
        char checksum[CHECKSUM_SIZE];
    } end_of_message;

    typedef struct
    {
        char type[MIN_SIZE];
        unsigned int id;
        unsigned int hub_id;
        unsigned int warehouse_id;
        char timestamp_requested[TIMESTAMP_SIZE];
        char timestamp_dispatched[TIMESTAMP_SIZE];
        char timestamp_received[TIMESTAMP_SIZE];
        inventory_item items[ITEM_TYPE];
        char origin[MIN_SIZE];
        char destination[MIN_SIZE];
        char checksum[CHECKSUM_SIZE];
    } server_transaction_history;

    typedef struct
    {
        char type[MIN_SIZE];
        char username[USER_PASS_SIZE];
        char client_type[MIN_SIZE];
        char status[MIN_SIZE];
        char checksum[CHECKSUM_SIZE];
    } server_client_alive;

    char* get_timestamp();
    int validate_checksum(const char* json_string);
    init_params_client load_config_client(const char* filename, const int index);
    client_auth_request deserialize_client_auth_request(const char* json_string);
    server_auth_response deserialize_server_auth_response(const char* json_string);
    client_keepalive deserialize_client_keepalive(const char* json_string);
    client_inventory_update deserialize_client_inventory_update(const char* json_string);
    server_emergency_alert deserialize_server_emergency_alert(const char* json_string);
    client_acknowledgment deserialize_client_acknowledgment(const char* json_string);
    client_emergency_alert deserialize_client_infection_alert(const char* json_string);
    server_w_stock_hub deserialize_server_w_stock_hub(const char* json_string);
    warehouse_send_stock_to_hub deserialize_warehouse_send_stock_to_hub(const char* json_string);
    warehouse_request_stock deserialize_warehouse_request_stock(const char* json_string);
    server_w_stock_warehouse deserialize_server_w_stock_warehouse(const char* json_string);
    server_h_request_delivery deserialize_sever_h_request_delivery(const char* json_string);
    hub_request_stock deserialize_hub_request_stock(const char* json_string);
    server_h_send_stock deserialize_server_h_send_stock(const char* json_string);
    cli_message deserialize_cli_message(const char* json_string);
    end_of_message deserialize_end_of_message(const char* json_string);
    server_transaction_history deserialize_server_transaction_history(const char* json_string);
    server_client_alive deserialize_server_client_alive(const char* json_string);

    char* get_type(const char* json_string);
    char* get_cli_type(const char* json_string);
    char* serialize_client_auth_request(const client_auth_request* client_auth_request);
    char* serialize_server_auth_response(const server_auth_response* server_auth_response);
    char* serialize_client_keepalive(const client_keepalive* client_keepalive);
    char* serialize_client_inventory_update(const client_inventory_update* client_inventory_update,
                                            const int item_count);
    char* serialize_server_emergency_alert(const server_emergency_alert* server_emergency_alert);
    char* serialize_client_acknowledgment(const client_acknowledgment* client_acknowledgment);
    char* serialize_client_infection_alert(const client_emergency_alert* client_emergency_alert);
    char* serialize_server_w_stock_hub(const server_w_stock_hub* server_w_stock_hub, const int item_count);
    char* serialize_warehouse_send_stock_to_hub(const warehouse_send_stock_to_hub* warehouse_send_stock_to_hub,
                                                const int item_count);
    char* serialize_warehouse_request_stock(const warehouse_request_stock* warehouse_request_stock,
                                            const int item_count);
    char* serialize_server_w_stock_warehouse(const server_w_stock_warehouse* server_w_stock_warehouse,
                                             const int item_count);
    char* serialize_hub_request_stock(const hub_request_stock* hub_request_stock, const int item_count);
    char* serialize_server_h_send_stock(const server_h_send_stock* server_h_send_stock, const int item_count);
    char* serialize_cli_message(const cli_message* cli_message);
    char* serialize_end_of_message(const end_of_message* end_of_message);
    char* serialize_server_transaction_history(const server_transaction_history* server_transaction_history,
                                               const int item_count);
    char* serialize_server_client_alive(const server_client_alive* server_client_alive);
    client_auth_request create_client_auth_request(const char* client_id, const char* client_type, const char* username,
                                                   const char* password);
    server_auth_response create_server_auth_response(const char* status, const char* session_token,
                                                     const char* message);
    client_keepalive create_client_keepalive(const char* username, const char* session_token);
    client_inventory_update create_client_inventory_update(const char* username, const char* session_token,
                                                           const inventory_item* items, const int item_count);
    server_emergency_alert create_server_emergency_alert(const char* alert_type);
    client_acknowledgment create_client_acknowledgment(const char* username, const char* session_token,
                                                       const char* status);
    client_emergency_alert create_client_infection_alert(const char* username, const char* session_token);
    server_w_stock_hub create_server_w_stock_hub(const inventory_item* items, const int item_count);
    warehouse_send_stock_to_hub create_warehouse_send_stock_to_hub(const char* username, const char* session_token,
                                                                   const inventory_item* items, const int item_count);
    warehouse_request_stock create_warehouse_request_stock(const char* username, const char* session_token,
                                                           const inventory_item* items, const int item_count);
    server_w_stock_warehouse create_server_w_stock_warehouse(const inventory_item* items, const int item_count);
    hub_request_stock create_hub_request_stock(const char* username, const char* session_token,
                                               const inventory_item* items, const int item_count);
    server_h_send_stock create_server_h_send_stock(const inventory_item* items, const int item_count);
    client_acknowledgment create_hub_receive_stock(const char* username, const char* session_token, const char* status);
    cli_message create_cli_message(const char* username, const char* session_token, const int type_message);
    end_of_message create_end_of_message();
    server_transaction_history create_server_transaction_history(const int id, const int hub_id, const int warehouse_id,
                                                                 const char* timestamp_requested,
                                                                 const char* timestamp_dispatched,
                                                                 const char* timestamp_received,
                                                                 const inventory_item* items, const char* origin,
                                                                 const char* destination);
    server_client_alive create_server_client_alive(const char* username, const char* type, const char* status);

#ifdef __cplusplus
}
#endif

#endif
