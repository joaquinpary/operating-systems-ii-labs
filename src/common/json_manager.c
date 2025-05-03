#define _GNU_SOURCE
#include "json_manager.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define WAREHOUSE "warehouse"
#define HUB "hub"

char* get_timestamp()
{
    char* timestamp = malloc(TIMESTAMP_SIZE);
    if (!timestamp)
        return NULL;

    time_t now = time(NULL);
    struct tm* t = gmtime(&now);
    strftime(timestamp, TIMESTAMP_SIZE, "%Y-%m-%dT%H:%M:%SZ", t);
    return timestamp;
}

int validate_checksum(const char* json_string)
{
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return 1;
    }

    // Extraer el checksum del JSON
    cJSON* checksum_item = cJSON_GetObjectItemCaseSensitive(json, "checksum");
    if (!checksum_item || !cJSON_IsString(checksum_item))
    {
        fprintf(stderr, "Checksum missing or invalid in JSON.\n");
        cJSON_Delete(json);
        return 1;
    }

    char expected_checksum[CHECKSUM_SIZE + 1];
    strncpy(expected_checksum, checksum_item->valuestring, CHECKSUM_SIZE);
    expected_checksum[CHECKSUM_SIZE] = '\0';
    cJSON_DeleteItemFromObject(json, "checksum");

    char* json_str_no_checksum = cJSON_PrintUnformatted(json);
    if (!json_str_no_checksum)
    {
        fprintf(stderr, "Error converting JSON to string.\n");
        cJSON_Delete(json);
        return 1;
    }
    uLong computed_crc = crc32(0L, Z_NULL, 0);
    computed_crc = crc32(computed_crc, (const Bytef*)json_str_no_checksum, strlen(json_str_no_checksum));
    char computed_checksum_str[CHECKSUM_SIZE + 1];
    snprintf(computed_checksum_str, sizeof(computed_checksum_str), "%08lX", computed_crc);
    int result = strcmp(expected_checksum, computed_checksum_str);

    if (result)
    {
        fprintf(stderr, "Checksum mismatch: Expected %s, but got %s\n", expected_checksum, computed_checksum_str);
    }

    free(json_str_no_checksum);
    cJSON_Delete(json);
    return result;
}

init_params_client load_config_client(const char* filename, const int index)
{
    init_params_client params = {0};
    FILE* file = fopen(filename, "r");
    if (!file)
    {
        perror("Error opening file");
        return params;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    char* data = malloc(file_size + 1);
    fread(data, 1, file_size, file);
    data[file_size] = '\0';
    fclose(file);

    cJSON* root = cJSON_Parse(data);
    if (!root)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        free(data);
        return params;
    }
    cJSON* clients = cJSON_GetObjectItemCaseSensitive(root, "clients");
    if (!cJSON_IsArray(clients))
    {
        fprintf(stderr, "Invalid JSON format: 'clients' is not an array.\n");
        cJSON_Delete(root);
        free(data);
        return params;
    }

    cJSON* client = cJSON_GetArrayItem(clients, index);
    if (!cJSON_IsObject(client))
    {
        fprintf(stderr, "Invalid JSON format: 'client' is not an object.\n");
        cJSON_Delete(root);
        free(data);
        return params;
    }

    cJSON* client_id = cJSON_GetObjectItemCaseSensitive(client, "client_id");
    cJSON* username = cJSON_GetObjectItemCaseSensitive(client, "username");
    cJSON* password = cJSON_GetObjectItemCaseSensitive(client, "password");
    cJSON* connect = cJSON_GetObjectItemCaseSensitive(client, "connect");

    if (cJSON_IsString(client_id))
    {
        if (strncmp(cJSON_GetStringValue(client_id), WAREHOUSE, 9) == 0)
        {
            strncpy(params.client_type, WAREHOUSE, MIN_SIZE - 1);
        }
        else if (strncmp(cJSON_GetStringValue(client_id), HUB, 9) == 0)
        {
            strncpy(params.client_type, HUB, MIN_SIZE - 1);
        }
        else
        {
            fprintf(stderr, "Invalid client type: %s\n", cJSON_GetStringValue(client_id));
            cJSON_Delete(root);
            free(data);
            return params;
        }
        strncpy(params.client_id, client_id->valuestring, MIN_SIZE - 1);
    }
    if (cJSON_IsString(username))
        strncpy(params.username, username->valuestring, USER_PASS_SIZE - 1);
    if (cJSON_IsString(password))
        strncpy(params.password, password->valuestring, USER_PASS_SIZE - 1);

    if (connect)
    {
        cJSON* host = cJSON_GetObjectItemCaseSensitive(connect, "host");
        cJSON* port = cJSON_GetObjectItemCaseSensitive(connect, "port");
        cJSON* protocol = cJSON_GetObjectItemCaseSensitive(connect, "protocol");
        cJSON* ip_version = cJSON_GetObjectItemCaseSensitive(connect, "ip_version");

        if (cJSON_IsString(host))
            strncpy(params.connection_params.host, host->valuestring, BUFFER_SIZE - 1);
        if (cJSON_IsString(port))
            strncpy(params.connection_params.port, port->valuestring, MIN_SIZE - 1);
        if (cJSON_IsString(protocol))
            strncpy(params.connection_params.protocol, protocol->valuestring, MIN_SIZE - 1);
        if (cJSON_IsString(ip_version))
            strncpy(params.connection_params.ip_version, ip_version->valuestring, MIN_SIZE - 1);
    }

    cJSON_Delete(root);
    free(data);
    return params;
}

client_auth_request deserialize_client_auth_request(const char* json_string)
{
    client_auth_request client_auth_request = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return client_auth_request;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");
    if (cJSON_IsString(type) && cJSON_IsObject(payload) && cJSON_IsString(checksum))
    {
        strncpy(client_auth_request.type, type->valuestring, MIN_SIZE - 1);
        strncpy(client_auth_request.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);

        cJSON* client_id = cJSON_GetObjectItemCaseSensitive(payload, "client_id");
        cJSON* payload_type = cJSON_GetObjectItemCaseSensitive(payload, "type");
        cJSON* username = cJSON_GetObjectItemCaseSensitive(payload, "username");
        cJSON* password = cJSON_GetObjectItemCaseSensitive(payload, "password");
        cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(payload, "timestamp");

        if (cJSON_IsString(client_id) && cJSON_IsString(payload_type) && cJSON_IsString(username) &&
            cJSON_IsString(password) && cJSON_IsString(timestamp))
        {
            strncpy(client_auth_request.payload.client_id, client_id->valuestring, MIN_SIZE - 1);
            strncpy(client_auth_request.payload.type, payload_type->valuestring, MIN_SIZE - 1);
            strncpy(client_auth_request.payload.username, username->valuestring, USER_PASS_SIZE - 1);
            strncpy(client_auth_request.payload.password, password->valuestring, USER_PASS_SIZE - 1);
            strncpy(client_auth_request.payload.timestamp, timestamp->valuestring, TIMESTAMP_SIZE - 1);
        }
    }
    else
    {
        fprintf(stderr, "Invalid JSON format.\n");
        return client_auth_request;
    }
    cJSON_Delete(json);
    return client_auth_request;
}

server_auth_response deserialize_server_auth_response(const char* json_string)
{
    server_auth_response server_auth_response = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return server_auth_response;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");
    if (cJSON_IsString(type) && cJSON_IsObject(payload) && cJSON_IsString(checksum))
    {
        strncpy(server_auth_response.type, type->valuestring, MIN_SIZE - 1);
        strncpy(server_auth_response.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);

        cJSON* status = cJSON_GetObjectItemCaseSensitive(payload, "status");
        cJSON* session_token = cJSON_GetObjectItemCaseSensitive(payload, "session_token");
        cJSON* message = cJSON_GetObjectItemCaseSensitive(payload, "message");
        cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(payload, "timestamp");

        if (cJSON_IsString(status) && cJSON_IsString(session_token) && cJSON_IsString(message) &&
            cJSON_IsString(timestamp))
        {
            strncpy(server_auth_response.payload.status, status->valuestring, MIN_SIZE - 1);
            strncpy(server_auth_response.payload.session_token, session_token->valuestring, SESSION_TOKEN_SIZE - 1);
            strncpy(server_auth_response.payload.message, message->valuestring, BUFFER_SIZE - 1);
            strncpy(server_auth_response.payload.timestamp, timestamp->valuestring, TIMESTAMP_SIZE - 1);
        }
    }
    else
    {
        fprintf(stderr, "Invalid JSON format.\n");
        return server_auth_response;
    }
    cJSON_Delete(json);
    return server_auth_response;
}

client_keepalive deserialize_client_keepalive(const char* json_string)
{
    client_keepalive client_keepalive = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return client_keepalive;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");
    if (cJSON_IsString(type) && cJSON_IsObject(payload) && cJSON_IsString(checksum))
    {
        strncpy(client_keepalive.type, type->valuestring, MIN_SIZE - 1);
        strncpy(client_keepalive.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);

        cJSON* username = cJSON_GetObjectItemCaseSensitive(payload, "username");
        cJSON* session_token = cJSON_GetObjectItemCaseSensitive(payload, "session_token");
        cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(payload, "timestamp");

        if (cJSON_IsString(username) && cJSON_IsString(session_token) && cJSON_IsString(timestamp))
        {
            strncpy(client_keepalive.payload.username, username->valuestring, USER_PASS_SIZE - 1);
            strncpy(client_keepalive.payload.session_token, session_token->valuestring, SESSION_TOKEN_SIZE - 1);
            strncpy(client_keepalive.payload.timestamp, timestamp->valuestring, TIMESTAMP_SIZE - 1);
        }
    }
    else
    {
        fprintf(stderr, "Invalid JSON format.\n");
        return client_keepalive;
    }
    cJSON_Delete(json);
    return client_keepalive;
}

client_inventory_update deserialize_client_inventory_update(const char* json_string)
{
    client_inventory_update client_inventory_update = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return client_inventory_update;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");
    if (cJSON_IsString(type) && cJSON_IsObject(payload) && cJSON_IsString(checksum))
    {
        strncpy(client_inventory_update.type, type->valuestring, MIN_SIZE - 1);
        strncpy(client_inventory_update.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);

        cJSON* username = cJSON_GetObjectItemCaseSensitive(payload, "username");
        cJSON* session_token = cJSON_GetObjectItemCaseSensitive(payload, "session_token");
        cJSON* items = cJSON_GetObjectItemCaseSensitive(payload, "items");
        cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(payload, "timestamp");

        if (cJSON_IsString(username) && cJSON_IsString(session_token) && cJSON_IsString(timestamp) &&
            cJSON_IsArray(items))
        {
            strncpy(client_inventory_update.payload.username, username->valuestring, USER_PASS_SIZE - 1);
            strncpy(client_inventory_update.payload.session_token, session_token->valuestring, SESSION_TOKEN_SIZE - 1);
            strncpy(client_inventory_update.payload.timestamp, timestamp->valuestring, TIMESTAMP_SIZE - 1);

            int item_count = cJSON_GetArraySize(items);
            for (int i = 0; i < item_count; i++)
            {
                cJSON* item = cJSON_GetArrayItem(items, i);
                if (cJSON_IsObject(item))
                {
                    cJSON* item_name = cJSON_GetObjectItemCaseSensitive(item, "item");
                    cJSON* quantity = cJSON_GetObjectItemCaseSensitive(item, "quantity");
                    if (cJSON_IsString(item_name) && cJSON_IsNumber(quantity))
                    {
                        strncpy(client_inventory_update.payload.items[i].item, item_name->valuestring, MIN_SIZE - 1);
                        client_inventory_update.payload.items[i].quantity = quantity->valueint;
                    }
                }
            }
        }
    }
    else
    {
        fprintf(stderr, "Invalid JSON format.\n");
        return client_inventory_update;
    }
    cJSON_Delete(json);
    return client_inventory_update;
}

server_emergency_alert deserialize_server_emergency_alert(const char* json_string)
{
    server_emergency_alert server_emergency_alert = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return server_emergency_alert;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");

    if (cJSON_IsString(type) && cJSON_IsObject(payload) && cJSON_IsString(checksum))
    {
        strncpy(server_emergency_alert.type, type->valuestring, MIN_SIZE - 1);
        strncpy(server_emergency_alert.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);

        cJSON* alert_type = cJSON_GetObjectItemCaseSensitive(payload, "alert_type");
        cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(payload, "timestamp");

        if (cJSON_IsString(alert_type) && cJSON_IsString(timestamp))
        {
            strncpy(server_emergency_alert.payload.timestamp, timestamp->valuestring, TIMESTAMP_SIZE - 1);
            strncpy(server_emergency_alert.payload.alert_type, alert_type->valuestring, MIN_SIZE - 1);
        }
    }
    else
    {
        fprintf(stderr, "Invalid JSON format.\n");
        return server_emergency_alert;
    }
    cJSON_Delete(json);
    return server_emergency_alert;
}

client_acknowledgment deserialize_client_acknowledgment(const char* json_string)
{
    client_acknowledgment client_acknowledgment = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return client_acknowledgment;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");

    if (cJSON_IsString(type) && cJSON_IsObject(payload) && cJSON_IsString(checksum))
    {
        strncpy(client_acknowledgment.type, type->valuestring, MIN_SIZE - 1);
        strncpy(client_acknowledgment.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);

        cJSON* username = cJSON_GetObjectItemCaseSensitive(payload, "username");
        cJSON* session_token = cJSON_GetObjectItemCaseSensitive(payload, "session_token");
        cJSON* status = cJSON_GetObjectItemCaseSensitive(payload, "status");
        cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(payload, "timestamp");

        if (cJSON_IsString(username) && cJSON_IsString(session_token) && cJSON_IsString(timestamp) &&
            cJSON_IsString(status))
        {
            strncpy(client_acknowledgment.payload.username, username->valuestring, USER_PASS_SIZE - 1);
            strncpy(client_acknowledgment.payload.session_token, session_token->valuestring, SESSION_TOKEN_SIZE - 1);
            strncpy(client_acknowledgment.payload.timestamp, timestamp->valuestring, TIMESTAMP_SIZE - 1);
            strncpy(client_acknowledgment.payload.status, status->valuestring, MIN_SIZE - 1);
        }
    }
    else
    {
        fprintf(stderr, "Invalid JSON format.\n");
        return client_acknowledgment;
    }
    cJSON_Delete(json);
    return client_acknowledgment;
}

client_infection_alert deserialize_client_infection_alert(const char* json_string)
{
    client_infection_alert client_infection_alert = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return client_infection_alert;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");

    if (cJSON_IsString(type) && cJSON_IsObject(payload) && cJSON_IsString(checksum))
    {
        strncpy(client_infection_alert.type, type->valuestring, MIN_SIZE - 1);
        strncpy(client_infection_alert.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);

        cJSON* username = cJSON_GetObjectItemCaseSensitive(payload, "username");
        cJSON* session_token = cJSON_GetObjectItemCaseSensitive(payload, "session_token");
        cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(payload, "timestamp");

        if (cJSON_IsString(username) && cJSON_IsString(session_token) && cJSON_IsString(timestamp))
        {
            strncpy(client_infection_alert.payload.username, username->valuestring, USER_PASS_SIZE - 1);
            strncpy(client_infection_alert.payload.session_token, session_token->valuestring, SESSION_TOKEN_SIZE - 1);
            strncpy(client_infection_alert.payload.timestamp, timestamp->valuestring, TIMESTAMP_SIZE - 1);
        }
    }
    else
    {
        fprintf(stderr, "Invalid JSON format.\n");
        return client_infection_alert;
    }
    cJSON_Delete(json);
    return client_infection_alert;
}

server_w_stock_hub deserialize_server_w_stock_hub(const char* json_string)
{
    server_w_stock_hub server_w_stock_hub = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return server_w_stock_hub;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");

    if (cJSON_IsString(type) && cJSON_IsObject(payload) && cJSON_IsString(checksum))
    {
        strncpy(server_w_stock_hub.type, type->valuestring, MIN_SIZE - 1);
        strncpy(server_w_stock_hub.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);

        cJSON* items = cJSON_GetObjectItemCaseSensitive(payload, "items");
        cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(payload, "timestamp");

        if (cJSON_IsArray(items) && cJSON_IsString(timestamp))
        {
            strncpy(server_w_stock_hub.payload.timestamp, timestamp->valuestring, TIMESTAMP_SIZE - 1);

            int item_count = cJSON_GetArraySize(items);
            for (int i = 0; i < item_count; i++)
            {
                cJSON* item = cJSON_GetArrayItem(items, i);
                if (cJSON_IsObject(item))
                {
                    cJSON* item_name = cJSON_GetObjectItemCaseSensitive(item, "item");
                    cJSON* quantity = cJSON_GetObjectItemCaseSensitive(item, "quantity");
                    if (cJSON_IsString(item_name) && cJSON_IsNumber(quantity))
                    {
                        strncpy(server_w_stock_hub.payload.items[i].item, item_name->valuestring, MIN_SIZE - 1);
                        server_w_stock_hub.payload.items[i].quantity = quantity->valueint;
                    }
                }
            }
        }
    }
    else
    {
        fprintf(stderr, "Invalid JSON format.\n");
        return server_w_stock_hub;
    }
    cJSON_Delete(json);
    return server_w_stock_hub;
}

warehouse_send_stock_to_hub deserialize_warehouse_send_stock_to_hub(const char* json_string)
{
    warehouse_send_stock_to_hub warehouse_send_stock_to_hub = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return warehouse_send_stock_to_hub;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");

    if (cJSON_IsString(type) && cJSON_IsObject(payload) && cJSON_IsString(checksum))
    {
        strncpy(warehouse_send_stock_to_hub.type, type->valuestring, MIN_SIZE - 1);
        strncpy(warehouse_send_stock_to_hub.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);

        cJSON* username = cJSON_GetObjectItemCaseSensitive(payload, "username");
        cJSON* session_token = cJSON_GetObjectItemCaseSensitive(payload, "session_token");
        cJSON* items = cJSON_GetObjectItemCaseSensitive(payload, "items");
        cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(payload, "timestamp");

        if (cJSON_IsString(username) && cJSON_IsString(session_token) && cJSON_IsArray(items) &&
            cJSON_IsString(timestamp))
        {
            strncpy(warehouse_send_stock_to_hub.payload.username, username->valuestring, USER_PASS_SIZE - 1);
            strncpy(warehouse_send_stock_to_hub.payload.session_token, session_token->valuestring,
                    SESSION_TOKEN_SIZE - 1);
            strncpy(warehouse_send_stock_to_hub.payload.timestamp, timestamp->valuestring, TIMESTAMP_SIZE - 1);

            int item_count = cJSON_GetArraySize(items);
            for (int i = 0; i < item_count; i++)
            {
                cJSON* item = cJSON_GetArrayItem(items, i);
                if (cJSON_IsObject(item))
                {
                    cJSON* item_name = cJSON_GetObjectItemCaseSensitive(item, "item");
                    cJSON* quantity = cJSON_GetObjectItemCaseSensitive(item, "quantity");
                    if (cJSON_IsString(item_name) && cJSON_IsNumber(quantity))
                    {
                        strncpy(warehouse_send_stock_to_hub.payload.items[i].item, item_name->valuestring,
                                MIN_SIZE - 1);
                        warehouse_send_stock_to_hub.payload.items[i].quantity = quantity->valueint;
                    }
                }
            }
        }
    }
    else
    {
        fprintf(stderr, "Invalid JSON format.\n");
        return warehouse_send_stock_to_hub;
    }
    cJSON_Delete(json);
    return warehouse_send_stock_to_hub;
}

warehouse_request_stock deserialize_warehouse_request_stock(const char* json_string)
{
    warehouse_request_stock warehouse_request_stock = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return warehouse_request_stock;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");

    if (cJSON_IsString(type) && cJSON_IsObject(payload) && cJSON_IsString(checksum))
    {
        strncpy(warehouse_request_stock.type, type->valuestring, MIN_SIZE - 1);
        strncpy(warehouse_request_stock.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);

        cJSON* username = cJSON_GetObjectItemCaseSensitive(payload, "username");
        cJSON* session_token = cJSON_GetObjectItemCaseSensitive(payload, "session_token");
        cJSON* items = cJSON_GetObjectItemCaseSensitive(payload, "items");
        cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(payload, "timestamp");

        if (cJSON_IsString(username) && cJSON_IsString(session_token) && cJSON_IsArray(items) &&
            cJSON_IsString(timestamp))
        {
            strncpy(warehouse_request_stock.payload.username, username->valuestring, USER_PASS_SIZE - 1);
            strncpy(warehouse_request_stock.payload.session_token, session_token->valuestring, SESSION_TOKEN_SIZE - 1);
            strncpy(warehouse_request_stock.payload.timestamp, timestamp->valuestring, TIMESTAMP_SIZE - 1);

            int item_count = cJSON_GetArraySize(items);
            for (int i = 0; i < item_count; i++)
            {
                cJSON* item = cJSON_GetArrayItem(items, i);
                if (cJSON_IsObject(item))
                {
                    cJSON* item_name = cJSON_GetObjectItemCaseSensitive(item, "item");
                    cJSON* quantity = cJSON_GetObjectItemCaseSensitive(item, "quantity");
                    if (cJSON_IsString(item_name) && cJSON_IsNumber(quantity))
                    {
                        strncpy(warehouse_request_stock.payload.items[i].item, item_name->valuestring, MIN_SIZE - 1);
                        warehouse_request_stock.payload.items[i].quantity = quantity->valueint;
                    }
                }
            }
        }
    }
    else
    {
        fprintf(stderr, "Invalid JSON format.\n");
        return warehouse_request_stock;
    }
    cJSON_Delete(json);
    return warehouse_request_stock;
}

server_w_stock_warehouse deserialize_server_w_stock_warehouse(const char* json_string)
{
    server_w_stock_warehouse server_w_stock_warehouse = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return server_w_stock_warehouse;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");

    if (cJSON_IsString(type) && cJSON_IsObject(payload) && cJSON_IsString(checksum))
    {
        strncpy(server_w_stock_warehouse.type, type->valuestring, MIN_SIZE - 1);
        strncpy(server_w_stock_warehouse.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);

        cJSON* items = cJSON_GetObjectItemCaseSensitive(payload, "items");
        cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(payload, "timestamp");

        if (cJSON_IsArray(items) && cJSON_IsString(timestamp))
        {
            strncpy(server_w_stock_warehouse.payload.timestamp, timestamp->valuestring, TIMESTAMP_SIZE - 1);

            int item_count = cJSON_GetArraySize(items);
            for (int i = 0; i < item_count; i++)
            {
                cJSON* item = cJSON_GetArrayItem(items, i);
                if (cJSON_IsObject(item))
                {
                    cJSON* item_name = cJSON_GetObjectItemCaseSensitive(item, "item");
                    cJSON* quantity = cJSON_GetObjectItemCaseSensitive(item, "quantity");
                    if (cJSON_IsString(item_name) && cJSON_IsNumber(quantity))
                    {
                        strncpy(server_w_stock_warehouse.payload.items[i].item, item_name->valuestring, MIN_SIZE - 1);
                        server_w_stock_warehouse.payload.items[i].quantity = quantity->valueint;
                    }
                }
            }
        }
    }
    else
    {
        fprintf(stderr, "Invalid JSON format.\n");
        return server_w_stock_warehouse;
    }
    cJSON_Delete(json);
    return server_w_stock_warehouse;
}

// to be implemented server_h_request_delivery deserialize_sever_h_request_delivery(const char* json_string);

hub_request_stock deserialize_hub_request_stock(const char* json_string)
{
    hub_request_stock hub_request_stock = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return hub_request_stock;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");

    if (cJSON_IsString(type) && cJSON_IsObject(payload) && cJSON_IsString(checksum))
    {
        strncpy(hub_request_stock.type, type->valuestring, MIN_SIZE - 1);
        strncpy(hub_request_stock.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);

        cJSON* username = cJSON_GetObjectItemCaseSensitive(payload, "username");
        cJSON* session_token = cJSON_GetObjectItemCaseSensitive(payload, "session_token");
        cJSON* items = cJSON_GetObjectItemCaseSensitive(payload, "items");
        cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(payload, "timestamp");

        if (cJSON_IsString(username) && cJSON_IsString(session_token) && cJSON_IsArray(items) &&
            cJSON_IsString(timestamp))
        {
            strncpy(hub_request_stock.payload.username, username->valuestring, USER_PASS_SIZE - 1);
            strncpy(hub_request_stock.payload.session_token, session_token->valuestring, SESSION_TOKEN_SIZE - 1);
            strncpy(hub_request_stock.payload.timestamp, timestamp->valuestring, TIMESTAMP_SIZE - 1);

            int item_count = cJSON_GetArraySize(items);
            for (int i = 0; i < item_count; i++)
            {
                cJSON* item = cJSON_GetArrayItem(items, i);
                if (cJSON_IsObject(item))
                {
                    cJSON* item_name = cJSON_GetObjectItemCaseSensitive(item, "item");
                    cJSON* quantity = cJSON_GetObjectItemCaseSensitive(item, "quantity");
                    if (cJSON_IsString(item_name) && cJSON_IsNumber(quantity))
                    {
                        strncpy(hub_request_stock.payload.items[i].item, item_name->valuestring, MIN_SIZE - 1);
                        hub_request_stock.payload.items[i].quantity = quantity->valueint;
                    }
                }
            }
        }
    }
    else
    {
        fprintf(stderr, "Invalid JSON format.\n");
        return hub_request_stock;
    }
    cJSON_Delete(json);
    return hub_request_stock;
}

server_h_send_stock deserialize_server_h_send_stock(const char* json_string)
{
    server_h_send_stock server_h_send_stock = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return server_h_send_stock;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");

    if (cJSON_IsString(type) && cJSON_IsObject(payload) && cJSON_IsString(checksum))
    {
        strncpy(server_h_send_stock.type, type->valuestring, MIN_SIZE - 1);
        strncpy(server_h_send_stock.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);

        cJSON* items = cJSON_GetObjectItemCaseSensitive(payload, "items");
        cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(payload, "timestamp");

        if (cJSON_IsArray(items) && cJSON_IsString(timestamp))
        {
            strncpy(server_h_send_stock.payload.timestamp, timestamp->valuestring, TIMESTAMP_SIZE - 1);

            int item_count = cJSON_GetArraySize(items);
            for (int i = 0; i < item_count; i++)
            {
                cJSON* item = cJSON_GetArrayItem(items, i);
                if (cJSON_IsObject(item))
                {
                    cJSON* item_name = cJSON_GetObjectItemCaseSensitive(item, "item");
                    cJSON* quantity = cJSON_GetObjectItemCaseSensitive(item, "quantity");
                    if (cJSON_IsString(item_name) && cJSON_IsNumber(quantity))
                    {
                        strncpy(server_h_send_stock.payload.items[i].item, item_name->valuestring, MIN_SIZE - 1);
                        server_h_send_stock.payload.items[i].quantity = quantity->valueint;
                    }
                }
            }
        }
    }
    else
    {
        fprintf(stderr, "Invalid JSON format.\n");
        return server_h_send_stock;
    }
    cJSON_Delete(json);
    return server_h_send_stock;
}

cli_message deserialize_cli_message(const char* json_string)
{
    cli_message cli_message = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return cli_message;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* username = cJSON_GetObjectItemCaseSensitive(json, "username");
    cJSON* session_token = cJSON_GetObjectItemCaseSensitive(json, "session_token");
    cJSON* message_type = cJSON_GetObjectItemCaseSensitive(json, "message_type");
    cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(json, "timestamp");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");

    if (cJSON_IsString(type) && cJSON_IsString(username) && cJSON_IsString(session_token) &&
        cJSON_IsString(message_type) && cJSON_IsString(timestamp) && cJSON_IsString(checksum))
    {
        strncpy(cli_message.type, type->valuestring, MIN_SIZE - 1);
        strncpy(cli_message.username, username->valuestring, USER_PASS_SIZE - 1);
        strncpy(cli_message.session_token, session_token->valuestring, SESSION_TOKEN_SIZE - 1);
        strncpy(cli_message.message_type, message_type->valuestring, MIN_SIZE - 1);
        strncpy(cli_message.timestamp, timestamp->valuestring, TIMESTAMP_SIZE - 1);
        strncpy(cli_message.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);
    }
    else
    {
        fprintf(stderr, "Invalid JSON format.\n");
        return cli_message;
    }

    cJSON_Delete(json);
    return cli_message;
}

end_of_message deserialize_end_of_message(const char* json_string)
{
    end_of_message end_of_message = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return end_of_message;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* timestamp = cJSON_GetObjectItemCaseSensitive(json, "timestamp");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");

    if (cJSON_IsString(type) && cJSON_IsString(checksum) && cJSON_IsString(timestamp))
    {
        strncpy(end_of_message.timestamp, timestamp->valuestring, TIMESTAMP_SIZE - 1);
        strncpy(end_of_message.type, type->valuestring, MIN_SIZE - 1);
        strncpy(end_of_message.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);
    }
    else
    {
        fprintf(stderr, "Invalid JSON format.\n");
        return end_of_message;
    }

    cJSON_Delete(json);
    return end_of_message;
}

server_transaction_history deserialize_server_transaction_history(const char* json_string)
{
    server_transaction_history server_transaction_history = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return server_transaction_history;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* id = cJSON_GetObjectItemCaseSensitive(json, "id");
    cJSON* hub_id = cJSON_GetObjectItemCaseSensitive(json, "hub_id");
    cJSON* warehouse_id = cJSON_GetObjectItemCaseSensitive(json, "warehouse_id");
    cJSON* timestamp_requested = cJSON_GetObjectItemCaseSensitive(json, "timestamp_requested");
    cJSON* timestamp_dispatched = cJSON_GetObjectItemCaseSensitive(json, "timestamp_dispatched");
    cJSON* timestamp_received = cJSON_GetObjectItemCaseSensitive(json, "timestamp_received");
    cJSON* items = cJSON_GetObjectItemCaseSensitive(json, "items");
    cJSON* origin = cJSON_GetObjectItemCaseSensitive(json, "origin");
    cJSON* destination = cJSON_GetObjectItemCaseSensitive(json, "destination");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");

    if (cJSON_IsString(type))
        strncpy(server_transaction_history.type, type->valuestring, MIN_SIZE - 1);
    if (cJSON_IsNumber(id))
        server_transaction_history.id = id->valueint;
    if (cJSON_IsNumber(hub_id))
        server_transaction_history.hub_id = hub_id->valueint;
    if (cJSON_IsNumber(warehouse_id))
        server_transaction_history.warehouse_id = warehouse_id->valueint;
    if (cJSON_IsString(timestamp_requested))
        strncpy(server_transaction_history.timestamp_requested, timestamp_requested->valuestring, TIMESTAMP_SIZE - 1);
    if (cJSON_IsString(timestamp_dispatched))
        strncpy(server_transaction_history.timestamp_dispatched, timestamp_dispatched->valuestring, TIMESTAMP_SIZE - 1);
    if (cJSON_IsString(timestamp_received))
        strncpy(server_transaction_history.timestamp_received, timestamp_received->valuestring, TIMESTAMP_SIZE - 1);
    if (cJSON_IsArray(items))
    {
        int item_count = cJSON_GetArraySize(items);
        for (int i = 0; i < item_count; i++)
        {
            cJSON* item = cJSON_GetArrayItem(items, i);
            if (cJSON_IsObject(item))
            {
                cJSON* item_name = cJSON_GetObjectItemCaseSensitive(item, "item");
                cJSON* quantity = cJSON_GetObjectItemCaseSensitive(item, "quantity");
                if (cJSON_IsString(item_name) && cJSON_IsNumber(quantity))
                {
                    strncpy(server_transaction_history.items[i].item, item_name->valuestring, MIN_SIZE - 1);
                    server_transaction_history.items[i].quantity = quantity->valueint;
                }
            }
        }
    }
    if (cJSON_IsString(origin))
        strncpy(server_transaction_history.origin, origin->valuestring, MIN_SIZE - 1);
    if (cJSON_IsString(destination))
        strncpy(server_transaction_history.destination, destination->valuestring, MIN_SIZE - 1);
    if (cJSON_IsString(checksum))
        strncpy(server_transaction_history.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);
    cJSON_Delete(json);
    return server_transaction_history;
}

server_client_alive deserialize_server_client_alive(const char* json_string)
{
    server_client_alive server_client_alive = {0};
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return server_client_alive;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON* username = cJSON_GetObjectItemCaseSensitive(json, "username");
    cJSON* client_type = cJSON_GetObjectItemCaseSensitive(json, "client_type");
    cJSON* status = cJSON_GetObjectItemCaseSensitive(json, "status");
    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(json, "checksum");

    if (cJSON_IsString(type))
        strncpy(server_client_alive.type, type->valuestring, MIN_SIZE - 1);
    if (cJSON_IsString(username))
        strncpy(server_client_alive.username, username->valuestring, USER_PASS_SIZE - 1);
    if (cJSON_IsString(client_type))
        strncpy(server_client_alive.client_type, client_type->valuestring, MIN_SIZE - 1);
    if (cJSON_IsString(status))
        strncpy(server_client_alive.status, status->valuestring, MIN_SIZE - 1);
    if (cJSON_IsString(checksum))
        strncpy(server_client_alive.checksum, checksum->valuestring, CHECKSUM_SIZE - 1);

    cJSON_Delete(json);
    return server_client_alive;
}

char* get_type(const char* json_string)
{
    if (!json_string)
    {
        fprintf(stderr, "Error: json_string is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return NULL;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "type");
    if (!type || !cJSON_IsString(type))
    {
        fprintf(stderr, "Error: type not found or not a string\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* type_string = malloc(strlen(type->valuestring) + 1);
    if (!type_string)
    {
        fprintf(stderr, "Error allocating memory for type string\n");
        cJSON_Delete(json);
        return NULL;
    }
    strcpy(type_string, type->valuestring);
    cJSON_Delete(json);
    return type_string;
}

char* get_cli_type(const char* json_string)
{
    if (!json_string)
    {
        fprintf(stderr, "Error: json_string is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_Parse(json_string);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return NULL;
    }
    cJSON* type = cJSON_GetObjectItemCaseSensitive(json, "message_type");
    if (!type || !cJSON_IsString(type))
    {
        fprintf(stderr, "Error: type not found or not a string\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* type_string = malloc(strlen(type->valuestring) + 1);
    if (!type_string)
    {
        fprintf(stderr, "Error allocating memory for type string\n");
        cJSON_Delete(json);
        return NULL;
    }
    strcpy(type_string, type->valuestring);
    cJSON_Delete(json);
    return type_string;
}

char* serialize_client_auth_request(const client_auth_request* client_auth_request)
{
    if (!client_auth_request)
    {
        fprintf(stderr, "Error: client_auth_request is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", client_auth_request->type);

    cJSON* payload = cJSON_CreateObject();
    if (!payload)
    {
        fprintf(stderr, "Error creating payload JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON_AddStringToObject(payload, "client_id", client_auth_request->payload.client_id);
    cJSON_AddStringToObject(payload, "type", client_auth_request->payload.type);
    cJSON_AddStringToObject(payload, "username", client_auth_request->payload.username);
    cJSON_AddStringToObject(payload, "password", client_auth_request->payload.password);
    cJSON_AddStringToObject(payload, "timestamp", client_auth_request->payload.timestamp);
    cJSON_AddItemToObject(json, "payload", payload);
    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}

char* serialize_server_auth_response(const server_auth_response* server_auth_response)
{
    if (!server_auth_response)
    {
        fprintf(stderr, "Error: server_auth_response is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", server_auth_response->type);

    cJSON* payload = cJSON_CreateObject();
    if (!payload)
    {
        fprintf(stderr, "Error creating payload JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON_AddStringToObject(payload, "status", server_auth_response->payload.status);
    cJSON_AddStringToObject(payload, "session_token", server_auth_response->payload.session_token);
    cJSON_AddStringToObject(payload, "message", server_auth_response->payload.message);
    cJSON_AddStringToObject(payload, "timestamp", server_auth_response->payload.timestamp);
    cJSON_AddItemToObject(json, "payload", payload);
    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}

char* serialize_client_keepalive(const client_keepalive* client_keepalive)
{
    if (!client_keepalive)
    {
        fprintf(stderr, "Error: client_keepalive is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", client_keepalive->type);

    cJSON* payload = cJSON_CreateObject();
    if (!payload)
    {
        fprintf(stderr, "Error creating payload JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON_AddStringToObject(payload, "username", client_keepalive->payload.username);
    cJSON_AddStringToObject(payload, "session_token", client_keepalive->payload.session_token);
    cJSON_AddStringToObject(payload, "timestamp", client_keepalive->payload.timestamp);

    cJSON_AddItemToObject(json, "payload", payload);

    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}

char* serialize_client_inventory_update(const client_inventory_update* client_inventory_update, const int item_count)
{
    if (!client_inventory_update)
    {
        fprintf(stderr, "Error: client_inventory_update is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", client_inventory_update->type);

    cJSON* payload = cJSON_CreateObject();
    if (!payload)
    {
        fprintf(stderr, "Error creating payload JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON* items = cJSON_CreateArray();
    if (!items)
    {
        fprintf(stderr, "Error creating items JSON array\n");
        cJSON_Delete(json);
        return NULL;
    }

    for (int i = 0; i < item_count; i++)
    {
        cJSON* item = cJSON_CreateObject();
        if (!item)
        {
            fprintf(stderr, "Error creating item JSON object\n");
            cJSON_Delete(json);
            return NULL;
        }

        cJSON_AddStringToObject(item, "item", client_inventory_update->payload.items[i].item);
        cJSON_AddNumberToObject(item, "quantity", client_inventory_update->payload.items[i].quantity);
        cJSON_AddItemToArray(items, item);
    }

    cJSON_AddStringToObject(payload, "username", client_inventory_update->payload.username);
    cJSON_AddStringToObject(payload, "session_token", client_inventory_update->payload.session_token);
    cJSON_AddItemToObject(payload, "items", items);
    cJSON_AddStringToObject(payload, "timestamp", client_inventory_update->payload.timestamp);
    cJSON_AddItemToObject(json, "payload", payload);
    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}

char* serialize_server_emergency_alert(const server_emergency_alert* server_emergency_alert)
{
    if (!server_emergency_alert)
    {
        fprintf(stderr, "Error: server_emergency_alert is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", server_emergency_alert->type);

    cJSON* payload = cJSON_CreateObject();
    if (!payload)
    {
        fprintf(stderr, "Error creating payload JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON_AddStringToObject(payload, "alert_type", server_emergency_alert->payload.alert_type);
    cJSON_AddStringToObject(payload, "timestamp", server_emergency_alert->payload.timestamp);

    cJSON_AddItemToObject(json, "payload", payload);
    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}

char* serialize_client_acknowledgment(const client_acknowledgment* client_acknowledgment)
{
    if (!client_acknowledgment)
    {
        fprintf(stderr, "Error: client_acknowledgment is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", client_acknowledgment->type);

    cJSON* payload = cJSON_CreateObject();
    if (!payload)
    {
        fprintf(stderr, "Error creating payload JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON_AddStringToObject(payload, "username", client_acknowledgment->payload.username);
    cJSON_AddStringToObject(payload, "session_token", client_acknowledgment->payload.session_token);
    cJSON_AddStringToObject(payload, "status", client_acknowledgment->payload.status);
    cJSON_AddStringToObject(payload, "timestamp", client_acknowledgment->payload.timestamp);

    cJSON_AddItemToObject(json, "payload", payload);
    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}

char* serialize_client_infection_alert(const client_infection_alert* client_infection_alert)
{
    if (!client_infection_alert)
    {
        fprintf(stderr, "Error: client_infection_alert is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", client_infection_alert->type);

    cJSON* payload = cJSON_CreateObject();
    if (!payload)
    {
        fprintf(stderr, "Error creating payload JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON_AddStringToObject(payload, "username", client_infection_alert->payload.username);
    cJSON_AddStringToObject(payload, "session_token", client_infection_alert->payload.session_token);
    cJSON_AddStringToObject(payload, "timestamp", client_infection_alert->payload.timestamp);
    cJSON_AddItemToObject(json, "payload", payload);
    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}

char* serialize_server_w_stock_hub(const server_w_stock_hub* server_w_stock_hub, const int item_count)
{
    if (!server_w_stock_hub)
    {
        fprintf(stderr, "Error: server_w_stock_hub is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", server_w_stock_hub->type);

    cJSON* payload = cJSON_CreateObject();
    if (!payload)
    {
        fprintf(stderr, "Error creating payload JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON* items = cJSON_CreateArray();
    if (!items)
    {
        fprintf(stderr, "Error creating items JSON array\n");
        cJSON_Delete(json);
        return NULL;
    }

    for (int i = 0; i < item_count; i++)
    {
        if (strlen(server_w_stock_hub->payload.items[i].item) > 0)
        {
            cJSON* item = cJSON_CreateObject();
            if (!item)
            {
                fprintf(stderr, "Error creating item JSON object\n");
                cJSON_Delete(json);
                return NULL;
            }
            cJSON_AddStringToObject(item, "item", server_w_stock_hub->payload.items[i].item);
            cJSON_AddNumberToObject(item, "quantity", server_w_stock_hub->payload.items[i].quantity);
            cJSON_AddItemToArray(items, item);
        }
    }

    cJSON_AddItemToObject(payload, "items", items);
    cJSON_AddStringToObject(payload, "timestamp", server_w_stock_hub->payload.timestamp);
    cJSON_AddItemToObject(json, "payload", payload);
    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}

char* serialize_warehouse_send_stock_to_hub(const warehouse_send_stock_to_hub* warehouse_send_stock_to_hub,
                                            const int item_count)
{
    if (!warehouse_send_stock_to_hub)
    {
        fprintf(stderr, "Error: warehouse_send_stock_to_hub is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", warehouse_send_stock_to_hub->type);

    cJSON* payload = cJSON_CreateObject();
    if (!payload)
    {
        fprintf(stderr, "Error creating payload JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON* items = cJSON_CreateArray();
    if (!items)
    {
        fprintf(stderr, "Error creating items JSON array\n");
        cJSON_Delete(json);
        return NULL;
    }

    for (int i = 0; i < item_count; i++)
    {
        if (strlen(warehouse_send_stock_to_hub->payload.items[i].item) > 0)
        {
            cJSON* item = cJSON_CreateObject();
            if (!item)
            {
                fprintf(stderr, "Error creating item JSON object\n");
                cJSON_Delete(json);
                return NULL;
            }
            cJSON_AddStringToObject(item, "item", warehouse_send_stock_to_hub->payload.items[i].item);
            cJSON_AddNumberToObject(item, "quantity", warehouse_send_stock_to_hub->payload.items[i].quantity);
            cJSON_AddItemToArray(items, item);
        }
    }

    cJSON_AddStringToObject(payload, "username", warehouse_send_stock_to_hub->payload.username);
    cJSON_AddStringToObject(payload, "session_token", warehouse_send_stock_to_hub->payload.session_token);
    cJSON_AddItemToObject(payload, "items", items);
    cJSON_AddStringToObject(payload, "timestamp", warehouse_send_stock_to_hub->payload.timestamp);
    cJSON_AddItemToObject(json, "payload", payload);
    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}

char* serialize_warehouse_request_stock(const warehouse_request_stock* warehouse_request_stock, const int item_count)
{
    if (!warehouse_request_stock)
    {
        fprintf(stderr, "Error: warehouse_request_stock is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", warehouse_request_stock->type);

    cJSON* payload = cJSON_CreateObject();
    if (!payload)
    {
        fprintf(stderr, "Error creating payload JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON* items = cJSON_CreateArray();
    if (!items)
    {
        fprintf(stderr, "Error creating items JSON array\n");
        cJSON_Delete(json);
        return NULL;
    }

    for (int i = 0; i < item_count; i++)
    {
        if (strlen(warehouse_request_stock->payload.items[i].item) > 0)
        {
            cJSON* item = cJSON_CreateObject();
            if (!item)
            {
                fprintf(stderr, "Error creating item JSON object\n");
                cJSON_Delete(json);
                return NULL;
            }
            cJSON_AddStringToObject(item, "item", warehouse_request_stock->payload.items[i].item);
            cJSON_AddNumberToObject(item, "quantity", warehouse_request_stock->payload.items[i].quantity);
            cJSON_AddItemToArray(items, item);
        }
    }

    cJSON_AddStringToObject(payload, "username", warehouse_request_stock->payload.username);
    cJSON_AddStringToObject(payload, "session_token", warehouse_request_stock->payload.session_token);
    cJSON_AddItemToObject(payload, "items", items);
    cJSON_AddStringToObject(payload, "timestamp", warehouse_request_stock->payload.timestamp);
    cJSON_AddItemToObject(json, "payload", payload);
    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}

char* serialize_server_w_stock_warehouse(const server_w_stock_warehouse* server_w_stock_warehouse, const int item_count)
{
    if (!server_w_stock_warehouse)
    {
        fprintf(stderr, "Error: server_w_stock_warehouse is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", server_w_stock_warehouse->type);

    cJSON* payload = cJSON_CreateObject();
    if (!payload)
    {
        fprintf(stderr, "Error creating payload JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON* items = cJSON_CreateArray();
    if (!items)
    {
        fprintf(stderr, "Error creating items JSON array\n");
        cJSON_Delete(json);
        return NULL;
    }

    for (int i = 0; i < item_count; i++)
    {
        if (strlen(server_w_stock_warehouse->payload.items[i].item) > 0)
        {
            cJSON* item = cJSON_CreateObject();
            if (!item)
            {
                fprintf(stderr, "Error creating item JSON object\n");
                cJSON_Delete(json);
                return NULL;
            }
            cJSON_AddStringToObject(item, "item", server_w_stock_warehouse->payload.items[i].item);
            cJSON_AddNumberToObject(item, "quantity", server_w_stock_warehouse->payload.items[i].quantity);
            cJSON_AddItemToArray(items, item);
        }
    }

    cJSON_AddItemToObject(payload, "items", items);
    cJSON_AddStringToObject(payload, "timestamp", server_w_stock_warehouse->payload.timestamp);
    cJSON_AddItemToObject(json, "payload", payload);
    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}
// to be implemented  char* serialize_server_h_request_delivery(const server_h_request_delivery*
// server_h_request_delivery);

char* serialize_hub_request_stock(const hub_request_stock* hub_request_stock, const int item_count)
{
    if (!hub_request_stock)
    {
        fprintf(stderr, "Error: hub_request_stock is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", hub_request_stock->type);

    cJSON* payload = cJSON_CreateObject();
    if (!payload)
    {
        fprintf(stderr, "Error creating payload JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON* items = cJSON_CreateArray();
    if (!items)
    {
        fprintf(stderr, "Error creating items JSON array\n");
        cJSON_Delete(json);
        return NULL;
    }

    for (int i = 0; i < item_count; i++)
    {
        if (strlen(hub_request_stock->payload.items[i].item) > 0)
        {
            cJSON* item = cJSON_CreateObject();
            if (!item)
            {
                fprintf(stderr, "Error creating item JSON object\n");
                cJSON_Delete(json);
                return NULL;
            }
            cJSON_AddStringToObject(item, "item", hub_request_stock->payload.items[i].item);
            cJSON_AddNumberToObject(item, "quantity", hub_request_stock->payload.items[i].quantity);
            cJSON_AddItemToArray(items, item);
        }
    }

    cJSON_AddStringToObject(payload, "username", hub_request_stock->payload.username);
    cJSON_AddStringToObject(payload, "session_token", hub_request_stock->payload.session_token);
    cJSON_AddItemToObject(payload, "items", items);
    cJSON_AddStringToObject(payload, "timestamp", hub_request_stock->payload.timestamp);
    cJSON_AddItemToObject(json, "payload", payload);
    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}

char* serialize_server_h_send_stock(const server_h_send_stock* server_h_send_stock, const int item_count)
{
    if (!server_h_send_stock)
    {
        fprintf(stderr, "Error: server_h_send_stock is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", server_h_send_stock->type);

    cJSON* payload = cJSON_CreateObject();
    if (!payload)
    {
        fprintf(stderr, "Error creating payload JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON* items = cJSON_CreateArray();
    if (!items)
    {
        fprintf(stderr, "Error creating items JSON array\n");
        cJSON_Delete(json);
        return NULL;
    }

    for (int i = 0; i < item_count; i++)
    {
        if (strlen(server_h_send_stock->payload.items[i].item) > 0)
        {
            cJSON* item = cJSON_CreateObject();
            if (!item)
            {
                fprintf(stderr, "Error creating item JSON object\n");
                cJSON_Delete(json);
                return NULL;
            }
            cJSON_AddStringToObject(item, "item", server_h_send_stock->payload.items[i].item);
            cJSON_AddNumberToObject(item, "quantity", server_h_send_stock->payload.items[i].quantity);
            cJSON_AddItemToArray(items, item);
        }
    }

    cJSON_AddItemToObject(payload, "items", items);
    cJSON_AddStringToObject(payload, "timestamp", server_h_send_stock->payload.timestamp);
    cJSON_AddItemToObject(json, "payload", payload);
    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}

char* serialize_cli_message(const cli_message* cli_message)
{
    if (!cli_message)
    {
        fprintf(stderr, "Error: cli_message is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", cli_message->type);
    cJSON_AddStringToObject(json, "username", cli_message->username);
    cJSON_AddStringToObject(json, "session_token", cli_message->session_token);
    cJSON_AddStringToObject(json, "message_type", cli_message->message_type);
    cJSON_AddStringToObject(json, "timestamp", cli_message->timestamp);

    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}

char* serialize_end_of_message(const end_of_message* end_of_message)
{
    if (!end_of_message)
    {
        fprintf(stderr, "Error: end_of_message is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", end_of_message->type);
    cJSON_AddStringToObject(json, "timestamp", end_of_message->timestamp);

    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}

char* serialize_server_transaction_history(const server_transaction_history* server_transaction_history,
                                           const int item_count)
{
    if (!server_transaction_history)
    {
        fprintf(stderr, "Error: server_transaction_history is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", server_transaction_history->type);
    cJSON_AddNumberToObject(json, "id", server_transaction_history->id);
    cJSON_AddNumberToObject(json, "hub_id", server_transaction_history->hub_id);
    cJSON_AddNumberToObject(json, "warehouse_id", server_transaction_history->warehouse_id);
    cJSON_AddStringToObject(json, "timestamp_requested", server_transaction_history->timestamp_requested);
    cJSON_AddStringToObject(json, "timestamp_dispatched", server_transaction_history->timestamp_dispatched);
    cJSON_AddStringToObject(json, "timestamp_received", server_transaction_history->timestamp_received);

    cJSON* items = cJSON_CreateArray();
    if (!items)
    {
        fprintf(stderr, "Error creating items JSON array\n");
        cJSON_Delete(json);
        return NULL;
    }

    for (int i = 0; i < item_count; i++)
    {
        if (strlen(server_transaction_history->items[i].item) > 0)
        {
            cJSON* item = cJSON_CreateObject();
            if (!item)
            {
                fprintf(stderr, "Error creating item JSON object\n");
                cJSON_Delete(json);
                return NULL;
            }
            cJSON_AddStringToObject(item, "item", server_transaction_history->items[i].item);
            cJSON_AddNumberToObject(item, "quantity", server_transaction_history->items[i].quantity);
            cJSON_AddItemToArray(items, item);
        }
    }

    cJSON_AddItemToObject(json, "items", items);
    cJSON_AddStringToObject(json, "origin", server_transaction_history->origin);
    cJSON_AddStringToObject(json, "destination", server_transaction_history->destination);
    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}

char* serialize_server_client_alive(const server_client_alive* server_client_alive)
{
    if (!server_client_alive)
    {
        fprintf(stderr, "Error: server_client_alive is NULL\n");
        return NULL;
    }
    cJSON* json = cJSON_CreateObject();
    if (!json)
    {
        fprintf(stderr, "Error creating JSON object\n");
        return NULL;
    }

    cJSON_AddStringToObject(json, "type", server_client_alive->type);
    cJSON_AddStringToObject(json, "username", server_client_alive->username);
    cJSON_AddStringToObject(json, "client_type", server_client_alive->client_type);
    cJSON_AddStringToObject(json, "status", server_client_alive->status);

    char* temp_json_str = cJSON_PrintUnformatted(json);
    if (!temp_json_str)
    {
        fprintf(stderr, "Error printing JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef*)temp_json_str, strlen(temp_json_str));
    free(temp_json_str);
    char checksum[CHECKSUM_SIZE];
    int checksum_length = snprintf(checksum, CHECKSUM_SIZE, "%08lX", crc);
    if (checksum_length >= CHECKSUM_SIZE)
    {
        fprintf(stderr, "Error: checksum buffer overflow\n");
        return NULL;
    }
    cJSON_AddStringToObject(json, "checksum", checksum);
    char* json_string = cJSON_PrintUnformatted(json);
    if (!json_string)
    {
        fprintf(stderr, "Error printing final JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    char* debug_json = cJSON_Print(json);
    if (!debug_json)
    {
        fprintf(stderr, "Error printing debug JSON object\n");
        cJSON_Delete(json);
        return NULL;
    }
    printf("Debug JSON: %s\n", debug_json);
    free(debug_json);
    cJSON_Delete(json);
    return json_string;
}

client_auth_request create_client_auth_request(const init_params_client params)
{
    client_auth_request aut_req = {.type = "client_auth_request", .checksum = ""};
    strncpy(aut_req.payload.client_id, params.client_id, MIN_SIZE - 1);
    aut_req.payload.client_id[MIN_SIZE - 1] = '\0';
    strncpy(aut_req.payload.type, params.client_type, MIN_SIZE - 1);
    aut_req.payload.type[MIN_SIZE - 1] = '\0';
    strncpy(aut_req.payload.username, params.username, USER_PASS_SIZE - 1);
    aut_req.payload.username[USER_PASS_SIZE - 1] = '\0';
    strncpy(aut_req.payload.password, params.password, USER_PASS_SIZE - 1);
    char* timestamp = get_timestamp();
    if (!timestamp)
    {
        fprintf(stderr, "Error generating timestamp\n");
        aut_req.payload.timestamp[0] = '\0';
    }
    strncpy(aut_req.payload.timestamp, timestamp, TIMESTAMP_SIZE - 1);
    aut_req.payload.timestamp[TIMESTAMP_SIZE - 1] = '\0';
    free(timestamp);
    return aut_req;
}

server_auth_response create_server_auth_response(const char* status, const char* session_token, const char* message)
{
    server_auth_response auth_res = {.type = "server_auth_response", .checksum = ""};
    strncpy(auth_res.payload.status, status, MIN_SIZE - 1);
    auth_res.payload.status[MIN_SIZE - 1] = '\0';
    strncpy(auth_res.payload.session_token, session_token, SESSION_TOKEN_SIZE - 1);
    auth_res.payload.session_token[SESSION_TOKEN_SIZE - 1] = '\0';
    strncpy(auth_res.payload.message, message, BUFFER_SIZE - 1);
    auth_res.payload.message[BUFFER_SIZE - 1] = '\0';
    char* timestamp = get_timestamp();
    if (!timestamp)
    {
        fprintf(stderr, "Error generating timestamp\n");
        auth_res.payload.timestamp[0] = '\0';
    }
    strncpy(auth_res.payload.timestamp, timestamp, TIMESTAMP_SIZE - 1);
    auth_res.payload.timestamp[TIMESTAMP_SIZE - 1] = '\0';
    free(timestamp);
    return auth_res;
}

client_keepalive create_client_keepalive(const char* username, const char* session_token)
{
    client_keepalive keepalive = {.type = "client_keepalive", .checksum = ""};
    strncpy(keepalive.payload.username, username, USER_PASS_SIZE - 1);
    keepalive.payload.username[USER_PASS_SIZE - 1] = '\0';
    strncpy(keepalive.payload.session_token, session_token, SESSION_TOKEN_SIZE - 1);
    keepalive.payload.session_token[SESSION_TOKEN_SIZE - 1] = '\0';
    char* timestamp = get_timestamp();
    if (!timestamp)
    {
        fprintf(stderr, "Error generating timestamp\n");
        keepalive.payload.timestamp[0] = '\0';
    }
    strncpy(keepalive.payload.timestamp, timestamp, TIMESTAMP_SIZE - 1);
    keepalive.payload.timestamp[TIMESTAMP_SIZE - 1] = '\0';
    free(timestamp);
    return keepalive;
}

client_inventory_update create_client_inventory_update(const char* username, const char* session_token,
                                                       const inventory_item* items, const int item_count)
{
    client_inventory_update inv_upd = {.type = "client_inventory_update", .checksum = ""};

    strncpy(inv_upd.payload.username, username, USER_PASS_SIZE - 1);
    inv_upd.payload.username[USER_PASS_SIZE - 1] = '\0';
    strncpy(inv_upd.payload.session_token, session_token, SESSION_TOKEN_SIZE - 1);
    inv_upd.payload.session_token[SESSION_TOKEN_SIZE - 1] = '\0';

    for (int i = 0; i < item_count; ++i)
    {
        strncpy(inv_upd.payload.items[i].item, items[i].item, MIN_SIZE - 1);
        inv_upd.payload.items[i].item[MIN_SIZE - 1] = '\0';
        inv_upd.payload.items[i].quantity = items[i].quantity;
    }

    char* timestamp = get_timestamp();
    if (!timestamp)
    {
        fprintf(stderr, "Error generating timestamp\n");
        inv_upd.payload.timestamp[0] = '\0';
    }
    strncpy(inv_upd.payload.timestamp, timestamp, TIMESTAMP_SIZE - 1);
    inv_upd.payload.timestamp[TIMESTAMP_SIZE - 1] = '\0';
    free(timestamp);
    return inv_upd;
}

server_emergency_alert create_server_emergency_alert(const char* alert_type)
{
    server_emergency_alert emerg_alert = {
        .type = "server_emergency_alert", .payload = {.alert_type = "", .timestamp = ""}, .checksum = ""};

    strncpy(emerg_alert.payload.alert_type, alert_type, MIN_SIZE - 1);
    emerg_alert.payload.alert_type[MIN_SIZE - 1] = '\0';
    char* timestamp = get_timestamp();
    if (!timestamp)
    {
        fprintf(stderr, "Error generating timestamp\n");
        emerg_alert.payload.timestamp[0] = '\0';
    }
    strncpy(emerg_alert.payload.timestamp, timestamp, TIMESTAMP_SIZE - 1);
    emerg_alert.payload.timestamp[TIMESTAMP_SIZE - 1] = '\0';
    free(timestamp);
    return emerg_alert;
}

client_acknowledgment create_client_acknowledgment(const char* username, const char* session_token, const char* status)
{
    client_acknowledgment ack = {.type = "client_acknowledgment", .checksum = ""};
    strncpy(ack.payload.username, username, USER_PASS_SIZE - 1);
    ack.payload.username[USER_PASS_SIZE - 1] = '\0';
    strncpy(ack.payload.session_token, session_token, SESSION_TOKEN_SIZE - 1);
    ack.payload.session_token[SESSION_TOKEN_SIZE - 1] = '\0';
    strncpy(ack.payload.status, status, MIN_SIZE - 1);
    ack.payload.status[MIN_SIZE - 1] = '\0';
    char* timestamp = get_timestamp();
    if (!timestamp)
    {
        fprintf(stderr, "Error generating timestamp\n");
        ack.payload.timestamp[0] = '\0';
    }
    strncpy(ack.payload.timestamp, timestamp, TIMESTAMP_SIZE - 1);
    ack.payload.timestamp[TIMESTAMP_SIZE - 1] = '\0';
    free(timestamp);
    return ack;
}

client_infection_alert create_client_infection_alert(const char* username, const char* session_token)
{
    client_infection_alert inf_alert = {.type = "client_infection_alert", .checksum = ""};
    strncpy(inf_alert.payload.username, username, USER_PASS_SIZE - 1);
    inf_alert.payload.username[USER_PASS_SIZE - 1] = '\0';
    strncpy(inf_alert.payload.session_token, session_token, SESSION_TOKEN_SIZE - 1);
    inf_alert.payload.session_token[SESSION_TOKEN_SIZE - 1] = '\0';
    char* timestamp = get_timestamp();
    if (!timestamp)
    {
        fprintf(stderr, "Error generating timestamp\n");
        inf_alert.payload.timestamp[0] = '\0';
    }
    strncpy(inf_alert.payload.timestamp, timestamp, TIMESTAMP_SIZE - 1);
    inf_alert.payload.timestamp[TIMESTAMP_SIZE - 1] = '\0';
    free(timestamp);
    return inf_alert;
}

server_w_stock_hub create_server_w_stock_hub(const inventory_item* items, const int item_count)
{
    server_w_stock_hub stock_hub = {
        .type = "server_w_stock_hub", .payload = {.items = {{0}}, .timestamp = ""}, .checksum = ""};

    for (int i = 0; i < item_count; ++i)
    {
        strncpy(stock_hub.payload.items[i].item, items[i].item, MIN_SIZE - 1);
        stock_hub.payload.items[i].item[MIN_SIZE - 1] = '\0';
        stock_hub.payload.items[i].quantity = items[i].quantity;
    }

    char* timestamp = get_timestamp();
    if (!timestamp)
    {
        fprintf(stderr, "Error generating timestamp\n");
        stock_hub.payload.timestamp[0] = '\0';
    }
    strncpy(stock_hub.payload.timestamp, timestamp, TIMESTAMP_SIZE - 1);
    stock_hub.payload.timestamp[TIMESTAMP_SIZE - 1] = '\0';
    free(timestamp);
    return stock_hub;
}

warehouse_send_stock_to_hub create_warehouse_send_stock_to_hub(const char* username, const char* session_token,
                                                               const inventory_item* items, const int item_count)
{
    warehouse_send_stock_to_hub stock_hub = {.type = "warehouse_send_stock_to_hub", .checksum = ""};
    strncpy(stock_hub.payload.username, username, USER_PASS_SIZE - 1);
    stock_hub.payload.username[USER_PASS_SIZE - 1] = '\0';
    strncpy(stock_hub.payload.session_token, session_token, SESSION_TOKEN_SIZE - 1);
    stock_hub.payload.session_token[SESSION_TOKEN_SIZE - 1] = '\0';
    for (int i = 0; i < item_count; ++i)
    {
        strncpy(stock_hub.payload.items[i].item, items[i].item, MIN_SIZE - 1);
        stock_hub.payload.items[i].item[MIN_SIZE - 1] = '\0';
        stock_hub.payload.items[i].quantity = items[i].quantity;
    }

    char* timestamp = get_timestamp();
    if (!timestamp)
    {
        fprintf(stderr, "Error generating timestamp\n");
        stock_hub.payload.timestamp[0] = '\0';
    }
    strncpy(stock_hub.payload.timestamp, timestamp, TIMESTAMP_SIZE - 1);
    stock_hub.payload.timestamp[TIMESTAMP_SIZE - 1] = '\0';
    free(timestamp);
    return stock_hub;
}

warehouse_request_stock create_warehouse_request_stock(const char* username, const char* session_token,
                                                       const inventory_item* items, const int item_count)
{
    warehouse_request_stock stock = {.type = "warehouse_request_stock", .checksum = ""};
    strncpy(stock.payload.username, username, USER_PASS_SIZE - 1);
    stock.payload.username[USER_PASS_SIZE - 1] = '\0';
    strncpy(stock.payload.session_token, session_token, SESSION_TOKEN_SIZE - 1);
    stock.payload.session_token[SESSION_TOKEN_SIZE - 1] = '\0';
    for (int i = 0; i < item_count; ++i)
    {
        strncpy(stock.payload.items[i].item, items[i].item, MIN_SIZE - 1);
        stock.payload.items[i].item[MIN_SIZE - 1] = '\0';
        stock.payload.items[i].quantity = items[i].quantity;
    }

    char* timestamp = get_timestamp();
    if (!timestamp)
    {
        fprintf(stderr, "Error generating timestamp\n");
        stock.payload.timestamp[0] = '\0';
    }
    strncpy(stock.payload.timestamp, timestamp, TIMESTAMP_SIZE - 1);
    stock.payload.timestamp[TIMESTAMP_SIZE - 1] = '\0';
    free(timestamp);
    return stock;
}

server_w_stock_warehouse create_server_w_stock_warehouse(const inventory_item* items, const int item_count)
{
    server_w_stock_warehouse stock_warehouse = {
        .type = "server_w_stock_warehouse", .payload = {.items = {{0}}, .timestamp = ""}, .checksum = ""};

    for (int i = 0; i < item_count; ++i)
    {
        strncpy(stock_warehouse.payload.items[i].item, items[i].item, MIN_SIZE - 1);
        stock_warehouse.payload.items[i].item[MIN_SIZE - 1] = '\0';
        stock_warehouse.payload.items[i].quantity = items[i].quantity;
    }

    for (int i = item_count; i < MIN_SIZE; ++i)
    {
        stock_warehouse.payload.items[i].item[0] = '\0';
        stock_warehouse.payload.items[i].quantity = 0;
    }

    char* timestamp = get_timestamp();
    if (!timestamp)
    {
        fprintf(stderr, "Error generating timestamp\n");
        stock_warehouse.payload.timestamp[0] = '\0';
    }
    strncpy(stock_warehouse.payload.timestamp, timestamp, TIMESTAMP_SIZE - 1);
    stock_warehouse.payload.timestamp[TIMESTAMP_SIZE - 1] = '\0';
    free(timestamp);
    return stock_warehouse;
}

// To be implemented server_h_request_delivery()

hub_request_stock create_hub_request_stock(const char* username, const char* session_tokken,
                                           const inventory_item* items, const int item_count)
{
    hub_request_stock stock = {.type = "hub_request_stock", .checksum = ""};
    strncpy(stock.payload.username, username, USER_PASS_SIZE - 1);
    stock.payload.username[USER_PASS_SIZE - 1] = '\0';
    strncpy(stock.payload.session_token, session_tokken, SESSION_TOKEN_SIZE - 1);
    stock.payload.session_token[SESSION_TOKEN_SIZE - 1] = '\0';
    for (int i = 0; i < item_count; ++i)
    {
        strncpy(stock.payload.items[i].item, items[i].item, MIN_SIZE - 1);
        stock.payload.items[i].item[MIN_SIZE - 1] = '\0';
        stock.payload.items[i].quantity = items[i].quantity;
    }

    char* timestamp = get_timestamp();
    if (!timestamp)
    {
        fprintf(stderr, "Error generating timestamp\n");
        stock.payload.timestamp[0] = '\0';
    }
    strncpy(stock.payload.timestamp, timestamp, TIMESTAMP_SIZE - 1);
    stock.payload.timestamp[TIMESTAMP_SIZE - 1] = '\0';
    free(timestamp);
    return stock;
}

server_h_send_stock create_server_h_send_stock(const inventory_item* items, const int item_count)
{
    server_h_send_stock stock = {
        .type = "server_h_send_stock", .payload = {.items = {{0}}, .timestamp = ""}, .checksum = ""};

    for (int i = 0; i < item_count; ++i)
    {
        strncpy(stock.payload.items[i].item, items[i].item, MIN_SIZE - 1);
        stock.payload.items[i].item[MIN_SIZE - 1] = '\0';
        stock.payload.items[i].quantity = items[i].quantity;
    }

    char* timestamp = get_timestamp();
    if (!timestamp)
    {
        fprintf(stderr, "Error generating timestamp\n");
        stock.payload.timestamp[0] = '\0';
    }
    strncpy(stock.payload.timestamp, timestamp, TIMESTAMP_SIZE - 1);
    stock.payload.timestamp[TIMESTAMP_SIZE - 1] = '\0';
    free(timestamp);
    return stock;
}

cli_message create_cli_message(const char* username, const char* session_token, const int type_message)
{
    cli_message msg = {.type = "cli_message",
                       .username = "",
                       .session_token = "",
                       .message_type = "",
                       .timestamp = "",
                       .checksum = ""};

    strncpy(msg.username, username, USER_PASS_SIZE - 1);
    msg.username[USER_PASS_SIZE - 1] = '\0';
    strncpy(msg.session_token, session_token, SESSION_TOKEN_SIZE - 1);
    msg.session_token[SESSION_TOKEN_SIZE - 1] = '\0';
    switch (type_message)
    {
    case 1:
        strncpy(msg.message_type, "transactions_history", MIN_SIZE - 1);
        break;
    case 2:
        strncpy(msg.message_type, "all_clients_live", MIN_SIZE - 1);
        break;
    case 3:
        strncpy(msg.message_type, "exit", MIN_SIZE - 1);
        break;
    default:
        break;
    }
    msg.message_type[MIN_SIZE - 1] = '\0';
    char* timestamp = get_timestamp();
    if (!timestamp)
    {
        fprintf(stderr, "Error generating timestamp\n");
        msg.timestamp[0] = '\0';
    }
    strncpy(msg.timestamp, timestamp, TIMESTAMP_SIZE - 1);
    msg.timestamp[TIMESTAMP_SIZE - 1] = '\0';
    free(timestamp);
    return msg;
}

end_of_message create_end_of_message()
{
    end_of_message msg = {.type = "end_of_message", .checksum = ""};
    char* timestamp = get_timestamp();
    if (!timestamp)
    {
        fprintf(stderr, "Error generating timestamp\n");
        msg.timestamp[0] = '\0';
    }
    strncpy(msg.timestamp, timestamp, TIMESTAMP_SIZE - 1);
    msg.timestamp[TIMESTAMP_SIZE - 1] = '\0';
    free(timestamp);
    return msg;
}

server_transaction_history create_server_transaction_history(const int id, const int hub_id, const int warehouse_id,
                                                             const char* timestamp_requested,
                                                             const char* timestamp_dispatched,
                                                             const char* timestamp_received,
                                                             const inventory_item* items, const char* origin,
                                                             const char* destination)
{
    server_transaction_history history = {
        .type = "server_transaction_history", .id = id, .hub_id = hub_id, .warehouse_id = warehouse_id, .checksum = ""};
    strncpy(history.timestamp_requested, timestamp_requested, TIMESTAMP_SIZE - 1);
    history.timestamp_requested[TIMESTAMP_SIZE - 1] = '\0';
    strncpy(history.timestamp_dispatched, timestamp_dispatched, TIMESTAMP_SIZE - 1);
    history.timestamp_dispatched[TIMESTAMP_SIZE - 1] = '\0';
    strncpy(history.timestamp_received, timestamp_received, TIMESTAMP_SIZE - 1);
    history.timestamp_received[TIMESTAMP_SIZE - 1] = '\0';
    // Modify later
    for (int i = 0; i < ITEM_TYPE; ++i)
    {
        strncpy(history.items[i].item, items[i].item, MIN_SIZE - 1);
        history.items[i].item[MIN_SIZE - 1] = '\0';
        history.items[i].quantity = items[i].quantity;
    }
    strncpy(history.origin, origin, MIN_SIZE - 1);
    history.origin[MIN_SIZE - 1] = '\0';
    strncpy(history.destination, destination, MIN_SIZE - 1);
    history.destination[MIN_SIZE - 1] = '\0';
    return history;
}

server_client_alive create_server_client_alive(const char* username, const char* type, const char* status)
{
    server_client_alive client_alive = {.type = "server_client_alive", .checksum = ""};
    strncpy(client_alive.username, username, USER_PASS_SIZE - 1);
    client_alive.username[USER_PASS_SIZE - 1] = '\0';
    strncpy(client_alive.client_type, type, MIN_SIZE - 1);
    client_alive.client_type[MIN_SIZE - 1] = '\0';
    strncpy(client_alive.status, status, MIN_SIZE - 1);
    client_alive.status[MIN_SIZE - 1] = '\0';
    return client_alive;
}
