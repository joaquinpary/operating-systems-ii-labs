#define _GNU_SOURCE
#include "json_manager.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <zlib.h>

static void safe_strcpy(char* dest, size_t size, const char* src)
{
    if (dest && size > 0)
    {
        strncpy(dest, src, size - 1);
        dest[size - 1] = '\0';
    }
}

static cJSON* serialize_items_list(const void* ptr)
{
    const payload_items_list* payload = (const payload_items_list*)ptr;
    cJSON* root = cJSON_CreateObject();
    cJSON* items_array = cJSON_CreateArray();

    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        if (payload->items[i].item_id == 0)
            continue;

        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "item_id", payload->items[i].item_id);
        cJSON_AddStringToObject(item, "item_name", payload->items[i].item_name);
        cJSON_AddNumberToObject(item, "quantity", payload->items[i].quantity);
        cJSON_AddItemToArray(items_array, item);
    }
    cJSON_AddItemToObject(root, "items", items_array);
    return root;
}

static cJSON* serialize_auth_response(const void* ptr)
{
    const payload_auth_response* payload = (const payload_auth_response*)ptr;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "status_code", payload->status_code);
    return root;
}

static cJSON* serialize_acknowledgment(const void* ptr)
{
    const payload_acknowledgment* payload = (const payload_acknowledgment*)ptr;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "status_code", payload->status_code);
    cJSON_AddStringToObject(root, "ack_for_timestamp", payload->ack_for_timestamp);
    return root;
}

static cJSON* serialize_auth_request(const void* ptr)
{
    const payload_auth_request* payload = (const payload_auth_request*)ptr;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "username", payload->username);
    cJSON_AddStringToObject(root, "password", payload->password);
    return root;
}

static cJSON* serialize_keepalive(const void* ptr)
{
    const payload_keepalive* payload = (const payload_keepalive*)ptr;
    cJSON* root = cJSON_CreateObject();
    char msg_str[2] = {payload->message, '\0'};
    cJSON_AddStringToObject(root, "message", msg_str);
    return root;
}

static cJSON* serialize_client_emergency(const void* ptr)
{
    const payload_client_emergency_alert* payload = (const payload_client_emergency_alert*)ptr;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "emergency_code", payload->emergency_code);
    cJSON_AddStringToObject(root, "emergency_type", payload->emergency_type);
    return root;
}

static cJSON* serialize_server_emergency(const void* ptr)
{
    const payload_server_emergency_alert* payload = (const payload_server_emergency_alert*)ptr;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "emergency_code", payload->emergency_code);
    cJSON_AddStringToObject(root, "instructions", payload->instructions);
    return root;
}

static void deserialize_items_list(const cJSON* root, void* ptr)
{
    payload_items_list* payload = (payload_items_list*)ptr;
    cJSON* items_array = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (cJSON_IsArray(items_array))
    {
        int i = 0;
        cJSON* item = NULL;
        cJSON_ArrayForEach(item, items_array)
        {
            if (i >= QUANTITY_ITEMS)
                break;

            cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "item_id");
            cJSON* name = cJSON_GetObjectItemCaseSensitive(item, "item_name");
            cJSON* qty = cJSON_GetObjectItemCaseSensitive(item, "quantity");

            if (cJSON_IsNumber(id))
                payload->items[i].item_id = id->valueint;
            if (cJSON_IsString(name))
                safe_strcpy(payload->items[i].item_name, ITEM_NAME_SIZE, name->valuestring);
            if (cJSON_IsNumber(qty))
                payload->items[i].quantity = qty->valueint;

            i++;
        }
    }
}

static void deserialize_auth_response(const cJSON* root, void* ptr)
{
    payload_auth_response* payload = (payload_auth_response*)ptr;
    cJSON* code = cJSON_GetObjectItemCaseSensitive(root, "status_code");
    if (cJSON_IsNumber(code))
    {
        payload->status_code = code->valueint;
    }
}

static void deserialize_acknowledgment(const cJSON* root, void* ptr)
{
    payload_acknowledgment* payload = (payload_acknowledgment*)ptr;
    cJSON* code = cJSON_GetObjectItemCaseSensitive(root, "status_code");
    cJSON* ack_ts = cJSON_GetObjectItemCaseSensitive(root, "ack_for_timestamp");
    
    if (cJSON_IsNumber(code))
    {
        payload->status_code = code->valueint;
    }
    if (cJSON_IsString(ack_ts))
    {
        safe_strcpy(payload->ack_for_timestamp, TIMESTAMP_SIZE, ack_ts->valuestring);
    }
}

static void deserialize_auth_request(const cJSON* root, void* ptr)
{
    payload_auth_request* payload = (payload_auth_request*)ptr;
    cJSON* user = cJSON_GetObjectItemCaseSensitive(root, "username");
    cJSON* pass = cJSON_GetObjectItemCaseSensitive(root, "password");

    if (cJSON_IsString(user))
        safe_strcpy(payload->username, CREDENTIALS_SIZE, user->valuestring);
    if (cJSON_IsString(pass))
        safe_strcpy(payload->password, CREDENTIALS_SIZE, pass->valuestring);
}

static void deserialize_keepalive(const cJSON* root, void* ptr)
{
    payload_keepalive* payload = (payload_keepalive*)ptr;
    cJSON* msg = cJSON_GetObjectItemCaseSensitive(root, "message");
    if (cJSON_IsString(msg) && strlen(msg->valuestring) > 0)
    {
        payload->message = msg->valuestring[0];
    }
}

static void deserialize_client_emergency(const cJSON* root, void* ptr)
{
    payload_client_emergency_alert* payload = (payload_client_emergency_alert*)ptr;
    cJSON* code = cJSON_GetObjectItemCaseSensitive(root, "emergency_code");
    cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "emergency_type");

    if (cJSON_IsNumber(code))
        payload->emergency_code = code->valueint;
    if (cJSON_IsString(type))
        safe_strcpy(payload->emergency_type, 20, type->valuestring);
}

static void deserialize_server_emergency(const cJSON* root, void* ptr)
{
    payload_server_emergency_alert* payload = (payload_server_emergency_alert*)ptr;
    cJSON* code = cJSON_GetObjectItemCaseSensitive(root, "emergency_code");
    cJSON* instr = cJSON_GetObjectItemCaseSensitive(root, "instructions");

    if (cJSON_IsNumber(code))
        payload->emergency_code = code->valueint;
    if (cJSON_IsString(instr))
        safe_strcpy(payload->instructions, 100, instr->valuestring);
}

typedef struct
{
    const char* key;
    cJSON* (*serialize)(const void*);
    void (*deserialize)(const cJSON*, void*);
} payload_handler_t;

static const payload_handler_t handlers[] = {
    {HUB_TO_SERVER__AUTH_REQUEST, serialize_auth_request, deserialize_auth_request},
    {WAREHOUSE_TO_SERVER__AUTH_REQUEST, serialize_auth_request, deserialize_auth_request},
    {SERVER_TO_HUB__AUTH_RESPONSE, serialize_auth_response, deserialize_auth_response},
    {SERVER_TO_WAREHOUSE__AUTH_RESPONSE, serialize_auth_response, deserialize_auth_response},
    {HUB_TO_SERVER__KEEPALIVE, serialize_keepalive, deserialize_keepalive},
    {WAREHOUSE_TO_SERVER__KEEPALIVE, serialize_keepalive, deserialize_keepalive},
    {HUB_TO_SERVER__INVENTORY_UPDATE, serialize_items_list, deserialize_items_list},
    {WAREHOUSE_TO_SERVER__INVENTORY_UPDATE, serialize_items_list, deserialize_items_list},
    {HUB_TO_SERVER__EMERGENCY_ALERT, serialize_client_emergency, deserialize_client_emergency},
    {WAREHOUSE_TO_SERVER__EMERGENCY_ALERT, serialize_client_emergency, deserialize_client_emergency},
    {HUB_TO_SERVER__STOCK_REQUEST, serialize_items_list, deserialize_items_list},
    {HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION, serialize_items_list, deserialize_items_list},
    {WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE, serialize_items_list, deserialize_items_list},
    {WAREHOUSE_TO_SERVER__REPLENISH_REQUEST, serialize_items_list, deserialize_items_list},
    {SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB, serialize_items_list, deserialize_items_list},
    {SERVER_TO_WAREHOUSE__RESTOCK_NOTICE, serialize_items_list, deserialize_items_list},
    {SERVER_TO_HUB__INCOMING_STOCK_NOTICE, serialize_items_list, deserialize_items_list},
    {SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT, serialize_server_emergency, deserialize_server_emergency},
    {HUB_TO_SERVER__ACK, serialize_acknowledgment, deserialize_acknowledgment},
    {WAREHOUSE_TO_SERVER__ACK, serialize_acknowledgment, deserialize_acknowledgment},
    {SERVER_TO_HUB__ACK, serialize_acknowledgment, deserialize_acknowledgment},
    {SERVER_TO_WAREHOUSE__ACK, serialize_acknowledgment, deserialize_acknowledgment},
    {NULL, NULL, NULL}};

static const payload_handler_t* find_handler(const char* msg_type)
{
    for (const payload_handler_t* h = handlers; h->key != NULL; h++)
    {
        if (strstr(msg_type, h->key))
        {
            return h;
        }
    }
    return NULL;
}

int serialize_message_to_json(const message_t* msg, char* out)
{
    if (!msg || !out || BUFFER_SIZE == 0)
        return -1;

    cJSON* root = cJSON_CreateObject();
    if (!root)
        return -2;

    cJSON_AddStringToObject(root, "msg_type", msg->msg_type);
    cJSON_AddStringToObject(root, "source_role", msg->source_role);
    cJSON_AddStringToObject(root, "source_id", msg->source_id);
    cJSON_AddStringToObject(root, "target_role", msg->target_role);
    cJSON_AddStringToObject(root, "target_id", msg->target_id);
    cJSON_AddStringToObject(root, "timestamp", msg->timestamp);

    const payload_handler_t* handler = find_handler(msg->msg_type);
    if (handler && handler->serialize)
    {
        cJSON* payload_json = handler->serialize(&msg->payload);
        if (payload_json)
        {
            cJSON_AddItemToObject(root, "payload", payload_json);
        }
    }

    cJSON_AddStringToObject(root, "checksum", msg->checksum);

    int ret = 0;
    if (!cJSON_PrintPreallocated(root, out, BUFFER_SIZE, 0))
    {
        ret = -3;
    }

    cJSON_Delete(root);
    return ret;
}

int deserialize_message_from_json(const char* json, message_t* out)
{
    if (!json || !out)
        return -1;

    cJSON* root = cJSON_Parse(json);
    if (!root)
        return -2;

    memset(out, 0, sizeof(message_t));

    cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "msg_type");
    cJSON* src_role = cJSON_GetObjectItemCaseSensitive(root, "source_role");
    cJSON* src_id = cJSON_GetObjectItemCaseSensitive(root, "source_id");
    cJSON* tgt_role = cJSON_GetObjectItemCaseSensitive(root, "target_role");
    cJSON* tgt_id = cJSON_GetObjectItemCaseSensitive(root, "target_id");
    cJSON* ts = cJSON_GetObjectItemCaseSensitive(root, "timestamp");
    cJSON* chk = cJSON_GetObjectItemCaseSensitive(root, "checksum");

    if (cJSON_IsString(type))
        safe_strcpy(out->msg_type, MESSAGE_TYPE_SIZE, type->valuestring);
    if (cJSON_IsString(src_role))
        safe_strcpy(out->source_role, ROLE_SIZE, src_role->valuestring);
    if (cJSON_IsString(src_id))
        safe_strcpy(out->source_id, ID_SIZE, src_id->valuestring);
    if (cJSON_IsString(tgt_role))
        safe_strcpy(out->target_role, ROLE_SIZE, tgt_role->valuestring);
    if (cJSON_IsString(tgt_id))
        safe_strcpy(out->target_id, ID_SIZE, tgt_id->valuestring);
    if (cJSON_IsString(ts))
        safe_strcpy(out->timestamp, TIMESTAMP_SIZE, ts->valuestring);
    if (cJSON_IsString(chk))
        safe_strcpy(out->checksum, CHECKSUM_SIZE, chk->valuestring);

    cJSON* payload_obj = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (payload_obj && cJSON_IsObject(payload_obj))
    {
        const payload_handler_t* handler = find_handler(out->msg_type);
        if (handler && handler->deserialize)
        {
            handler->deserialize(payload_obj, &out->payload);
        }
    }

    cJSON_Delete(root);
    return 0;
}

static void generate_timestamp(char* buffer, size_t size)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    struct tm* tm_info = gmtime(&tv.tv_sec);
    
    // Format: YYYY-MM-DDTHH:MM:SS.mmmZ (with milliseconds)
    char base[32];
    strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", tm_info);
    
    int milliseconds = tv.tv_usec / 1000;
    snprintf(buffer, size, "%s.%03dZ", base, milliseconds);
}

static void generate_checksum(const message_t* msg, char* checksum_out)
{
    unsigned long hash = 5381;
    const char* str = msg->msg_type;
    while (*str)
        hash = ((hash << 5) + hash) + (unsigned char)(*str++);
    str = msg->source_id;
    while (*str)
        hash = ((hash << 5) + hash) + (unsigned char)(*str++);

    snprintf(checksum_out, CHECKSUM_SIZE, "%lX", hash & 0xFFFFFF);
}

static int create_message(message_t* out, const char* msg_type, const char* source_role, const char* source_id,
                          const char* target_role, const char* target_id, const void* payload, size_t payload_size)
{
    if (!out || !msg_type || !source_role || !source_id || !target_role || !target_id)
        return -1;

    memset(out, 0, sizeof(message_t));

    safe_strcpy(out->msg_type, MESSAGE_TYPE_SIZE, msg_type);
    safe_strcpy(out->source_role, ROLE_SIZE, source_role);
    safe_strcpy(out->source_id, ID_SIZE, source_id);
    safe_strcpy(out->target_role, ROLE_SIZE, target_role);
    safe_strcpy(out->target_id, ID_SIZE, target_id);

    generate_timestamp(out->timestamp, TIMESTAMP_SIZE);

    if (payload && payload_size > 0 && payload_size <= sizeof(payload_t))
    {
        memcpy(&out->payload, payload, payload_size);
    }

    generate_checksum(out, out->checksum);

    return 0;
}

int create_auth_request_message(message_t* out, const char* source_role, const char* source_id, const char* username,
                                const char* password)
{
    if (!out || !source_role || !source_id || !username || !password)
        return -1;

    payload_auth_request payload = {0};
    safe_strcpy(payload.username, CREDENTIALS_SIZE, username);
    safe_strcpy(payload.password, CREDENTIALS_SIZE, password);

    const char* msg_type =
        (strcmp(source_role, HUB) == 0) ? HUB_TO_SERVER__AUTH_REQUEST : WAREHOUSE_TO_SERVER__AUTH_REQUEST;

    return create_message(out, msg_type, source_role, source_id, SERVER, SERVER, &payload, sizeof(payload));
}

int create_keepalive_message(message_t* out, const char* source_role, const char* source_id, char message)
{
    if (!out || !source_role || !source_id)
        return -1;

    payload_keepalive payload = {0};
    payload.message = message;

    const char* msg_type = (strcmp(source_role, HUB) == 0) ? HUB_TO_SERVER__KEEPALIVE : WAREHOUSE_TO_SERVER__KEEPALIVE;

    return create_message(out, msg_type, source_role, source_id, SERVER, SERVER, &payload, sizeof(payload));
}

int create_items_message(message_t* out, const char* source_role, const char* source_id, const char* target_role,
                         const char* target_id, const char* message_category, const inventory_item_t* items,
                         int item_count)
{
    if (!out || !source_role || !source_id || !target_role || !target_id || !message_category || !items ||
        item_count < 0)
        return -1;

    payload_items_list payload = {0};
    int count = (item_count > QUANTITY_ITEMS) ? QUANTITY_ITEMS : item_count;

    for (int i = 0; i < count; i++)
    {
        payload.items[i] = items[i];
    }

    const char* msg_type = NULL;

    if (strcmp(message_category, STOCK_REQUEST) == 0)
    {
        msg_type =
            (strcmp(source_role, HUB) == 0) ? HUB_TO_SERVER__STOCK_REQUEST : WAREHOUSE_TO_SERVER__REPLENISH_REQUEST;
    }
    else if (strcmp(message_category, INVENTORY_UPDATE) == 0)
    {
        msg_type =
            (strcmp(source_role, HUB) == 0) ? HUB_TO_SERVER__INVENTORY_UPDATE : WAREHOUSE_TO_SERVER__INVENTORY_UPDATE;
    }
    else if (strcmp(message_category, RECEIPT_CONFIRMATION) == 0)
    {
        msg_type = HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION;
    }
    else if (strcmp(message_category, SHIPMENT_NOTICE) == 0)
    {
        msg_type = WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE;
    }
    else if (strcmp(message_category, REPLENISH_REQUEST) == 0)
    {
        msg_type = WAREHOUSE_TO_SERVER__REPLENISH_REQUEST;
    }
    else if (strcmp(message_category, ORDER_DISPATCH) == 0)
    {
        msg_type = SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB;
    }
    else if (strcmp(message_category, RESTOCK_NOTICE) == 0)
    {
        msg_type = SERVER_TO_WAREHOUSE__RESTOCK_NOTICE;
    }
    else if (strcmp(message_category, INCOMING_STOCK_NOTICE) == 0)
    {
        msg_type = SERVER_TO_HUB__INCOMING_STOCK_NOTICE;
    }
    else
    {
        return -1; // Unknown category
    }

    return create_message(out, msg_type, source_role, source_id, target_role, target_id, &payload, sizeof(payload));
}

int create_auth_response_message(message_t* out, const char* target_role, const char* target_id, int status_code)
{
    if (!out || !target_role || !target_id)
        return -1;

    payload_auth_response payload = {0};
    payload.status_code = status_code;

    const char* msg_type = NULL;
    if (strcmp(target_role, HUB) == 0)
        msg_type = SERVER_TO_HUB__AUTH_RESPONSE;
    else if (strcmp(target_role, WAREHOUSE) == 0)
        msg_type = SERVER_TO_WAREHOUSE__AUTH_RESPONSE;

    if (!msg_type)
        return -1;

    return create_message(out, msg_type, SERVER, SERVER, target_role, target_id, &payload, sizeof(payload));
}

int create_acknowledgment_message(message_t* out, const char* source_role, const char* source_id,
                                 const char* target_role, const char* target_id,
                                 const char* ack_for_timestamp, int status_code)
{
    if (!out || !source_role || !source_id || !target_role || !target_id || !ack_for_timestamp)
        return -1;

    payload_acknowledgment payload = {0};
    payload.status_code = status_code;
    safe_strcpy(payload.ack_for_timestamp, TIMESTAMP_SIZE, ack_for_timestamp);

    const char* msg_type = NULL;

    if (strcmp(source_role, HUB) == 0 && strcmp(target_role, SERVER) == 0)
        msg_type = HUB_TO_SERVER__ACK;
    else if (strcmp(source_role, WAREHOUSE) == 0 && strcmp(target_role, SERVER) == 0)
        msg_type = WAREHOUSE_TO_SERVER__ACK;
    else if (strcmp(source_role, SERVER) == 0 && strcmp(target_role, HUB) == 0)
        msg_type = SERVER_TO_HUB__ACK;
    else if (strcmp(source_role, SERVER) == 0 && strcmp(target_role, WAREHOUSE) == 0)
        msg_type = SERVER_TO_WAREHOUSE__ACK;

    if (!msg_type)
        return -1;

    return create_message(out, msg_type, source_role, source_id, target_role, target_id, &payload, sizeof(payload));
}

int create_client_emergency_message(message_t* out, const char* source_role, const char* source_id, int emergency_code,
                                    const char* emergency_type)
{
    if (!out || !source_role || !source_id || !emergency_type)
        return -1;

    payload_client_emergency_alert payload = {0};
    payload.emergency_code = emergency_code;
    safe_strcpy(payload.emergency_type, 20, emergency_type);

    const char* msg_type =
        (strcmp(source_role, HUB) == 0) ? HUB_TO_SERVER__EMERGENCY_ALERT : WAREHOUSE_TO_SERVER__EMERGENCY_ALERT;

    return create_message(out, msg_type, source_role, source_id, SERVER, SERVER, &payload, sizeof(payload));
}

int create_server_emergency_message(message_t* out, int emergency_code, const char* instructions)
{
    if (!out || !instructions)
        return -1;

    payload_server_emergency_alert payload = {0};
    payload.emergency_code = emergency_code;
    safe_strcpy(payload.instructions, 100, instructions);

    // Placeholder
    return create_message(out, SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT, SERVER, "SERVER", CLI, "ALL", &payload,
                          sizeof(payload));
}
