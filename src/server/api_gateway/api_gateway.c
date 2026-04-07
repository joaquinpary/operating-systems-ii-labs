/**
 * @file api_gateway.c
 * @brief API Gateway plugin — loaded at runtime by the server via dlopen.
 *
 * Handles GATEWAY_TO_SERVER__COMMAND messages from the Go API gateway.
 * Self-contained: owns its own libpq connection so adding new gateway
 * commands never requires touching the server core.
 */

#include "api_gateway_interface.h"
#include "cJSON.h"

#include <libpq-fe.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <common/json_manager.h>

#define API_GATEWAY_VERSION "1.0.0"
#define CMD_BUF_SIZE 64

static PGconn* s_conn = NULL;

const char* api_gateway_version(void)
{
    return API_GATEWAY_VERSION;
}

int api_gateway_init(const char* conn_string)
{
    if (!conn_string)
        return -1;

    s_conn = PQconnectdb(conn_string);
    if (PQstatus(s_conn) != CONNECTION_OK)
    {
        fprintf(stderr, "[api_gateway] PQconnectdb failed: %s\n", PQerrorMessage(s_conn));
        PQfinish(s_conn);
        s_conn = NULL;
        return -1;
    }
    return 0;
}

void api_gateway_shutdown(void)
{
    if (s_conn)
    {
        PQfinish(s_conn);
        s_conn = NULL;
    }
}

/* Build a COMMAND_RESPONSE JSON string directly via cJSON.
 * Re-serialises from scratch because the helper functions
 * (generate_timestamp, generate_checksum) are static in json_manager.c. */
static int build_response_json(char* out, size_t max_len, const message_t* req, const char* status, cJSON* data)
{
    /* Build a response message using the public API. */
    message_t resp;
    memset(&resp, 0, sizeof(resp));

    /* Use create_acknowledgment_message just to get a properly initialised
     * message_t with timestamp + checksum. Then overwrite msg_type and payload. */
    create_acknowledgment_message(&resp, SERVER, SERVER, req->source_role, req->source_id, req->timestamp, OK);
    strncpy(resp.msg_type, SERVER_TO_GATEWAY__COMMAND_RESPONSE, MESSAGE_TYPE_SIZE - 1);
    resp.msg_type[MESSAGE_TYPE_SIZE - 1] = '\0';

    /* Serialise the envelope via the public API (gives us timestamp + checksum). */
    char envelope_buf[BUFFER_SIZE];
    if (serialize_message_to_json(&resp, envelope_buf) != 0)
        return -1;

    /* Parse it, replace the payload with our custom one. */
    cJSON* root = cJSON_Parse(envelope_buf);
    if (!root)
        return -1;

    cJSON_DeleteItemFromObject(root, "payload");
    cJSON* payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "status", status);
    if (data)
        cJSON_AddItemToObject(payload, "data", data);
    cJSON_AddItemToObject(root, "payload", payload);

    char* str = cJSON_PrintUnformatted(root);
    if (str)
    {
        snprintf(out, max_len, "%s", str);
        free(str);
    }
    cJSON_Delete(root);
    return 0;
}

static int build_error(char* out, size_t max_len, const message_t* req, const char* message)
{
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "message", message);
    return build_response_json(out, max_len, req, "error", data);
}

static int cmd_ping(char* out, size_t max_len, const message_t* req)
{
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "message", "pong");
    return build_response_json(out, max_len, req, "ok", data);
}

static const char* s_item_names[] = {"food", "water", "medicine", "tools", "guns", "ammo"};

static int cmd_create_shipment(char* out, size_t max_len, const message_t* req, cJSON* payload,
                               gateway_side_effect_t* side)
{
    if (!s_conn)
        return build_error(out, max_len, req, "database not connected");

    /* Parse items from payload. */
    cJSON* items_arr = cJSON_GetObjectItemCaseSensitive(payload, "items");
    if (!cJSON_IsArray(items_arr) || cJSON_GetArraySize(items_arr) == 0)
        return build_error(out, max_len, req, "items array required");

    int quantities[QUANTITY_ITEMS] = {0};
    cJSON* item = NULL;
    cJSON_ArrayForEach(item, items_arr)
    {
        cJSON* id_json = cJSON_GetObjectItemCaseSensitive(item, "item_id");
        cJSON* qty_json = cJSON_GetObjectItemCaseSensitive(item, "quantity");
        if (!cJSON_IsNumber(id_json) || !cJSON_IsNumber(qty_json))
            continue;
        int id = id_json->valueint;
        if (id >= 1 && id <= QUANTITY_ITEMS)
            quantities[id - 1] = qty_json->valueint;
    }

    /* Find a hub with sufficient stock (random pick). */
    const char* find_sql =
        "SELECT client_id FROM client_inventory "
        "WHERE client_type = 'HUB' "
        "  AND food >= $1 AND water >= $2 AND medicine >= $3 "
        "  AND tools >= $4 AND guns >= $5 AND ammo >= $6 "
        "OFFSET floor(random() * ("
        "  SELECT COUNT(*) FROM client_inventory "
        "  WHERE client_type = 'HUB' "
        "    AND food >= $1 AND water >= $2 AND medicine >= $3 "
        "    AND tools >= $4 AND guns >= $5 AND ammo >= $6"
        ")) LIMIT 1";

    char q[QUANTITY_ITEMS][16];
    const char* params[QUANTITY_ITEMS];
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        snprintf(q[i], sizeof(q[i]), "%d", quantities[i]);
        params[i] = q[i];
    }

    PGresult* res = PQexecParams(s_conn, find_sql, QUANTITY_ITEMS, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return build_error(out, max_len, req, "no hub with sufficient stock");
    }

    char hub_id[ID_SIZE];
    strncpy(hub_id, PQgetvalue(res, 0, 0), sizeof(hub_id) - 1);
    hub_id[sizeof(hub_id) - 1] = '\0';
    PQclear(res);

    /* Create transaction: source = hub, destination = EXTERNAL_CLIENT. */
    const char* txn_sql =
        "INSERT INTO inventory_transactions "
        "(transaction_type, source_id, source_type, destination_id, destination_type, "
        " status, food, water, medicine, tools, guns, ammo) "
        "VALUES ('ORDER_DISPATCH', $1, 'HUB', 'EXTERNAL_CLIENT', 'EXTERNAL_CLIENT', "
        " 'PENDING', $2, $3, $4, $5, $6, $7) "
        "RETURNING transaction_id";

    const char* txn_params[7] = {hub_id, q[0], q[1], q[2], q[3], q[4], q[5]};
    PGresult* txn_res = PQexecParams(s_conn, txn_sql, 7, NULL, txn_params, NULL, NULL, 0);
    if (PQresultStatus(txn_res) != PGRES_TUPLES_OK || PQntuples(txn_res) == 0)
    {
        PQclear(txn_res);
        return build_error(out, max_len, req, "failed to create transaction");
    }

    int transaction_id = atoi(PQgetvalue(txn_res, 0, 0));
    PQclear(txn_res);

    /* Build the dispatch message for the hub (side-effect). */
    if (side)
    {
        inventory_item_t items[QUANTITY_ITEMS];
        memset(items, 0, sizeof(items));
        int item_count = 0;
        for (int i = 0; i < QUANTITY_ITEMS; i++)
        {
            if (quantities[i] > 0)
            {
                items[item_count].item_id = i + 1;
                strncpy(items[item_count].item_name, s_item_names[i], ITEM_NAME_SIZE - 1);
                items[item_count].quantity = quantities[i];
                item_count++;
            }
        }

        message_t dispatch_msg;
        create_items_message(&dispatch_msg, SERVER_TO_HUB__ORDER_TO_DISPATCH_STOCK,
                             SERVER, hub_id, items, item_count, NULL);

        if (serialize_message_to_json(&dispatch_msg, side->send_json) == 0)
        {
            strncpy(side->target_username, hub_id, sizeof(side->target_username) - 1);
            side->has_message = 1;
        }
    }

    /* Build success response for the gateway. */
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "dispatch_hub_id", hub_id);
    cJSON_AddNumberToObject(data, "transaction_id", transaction_id);

    return build_response_json(out, max_len, req, "ok", data);
}

static int cmd_get_shipment_status(char* out, size_t max_len, const message_t* req, cJSON* payload)
{
    if (!s_conn)
        return build_error(out, max_len, req, "database not connected");

    cJSON* args_json = cJSON_GetObjectItemCaseSensitive(payload, "args");
    if (!cJSON_IsString(args_json) || !args_json->valuestring || args_json->valuestring[0] == '\0')
        return build_error(out, max_len, req, "args (transaction_id) required");

    const char* transaction_id_str = args_json->valuestring;

    const char* sql =
        "SELECT status FROM inventory_transactions WHERE transaction_id = $1";
    const char* params[] = {transaction_id_str};

    PGresult* res = PQexecParams(s_conn, sql, 1, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return build_error(out, max_len, req, "transaction not found");
    }

    char db_status[64] = {0};
    strncpy(db_status, PQgetvalue(res, 0, 0), sizeof(db_status) - 1);
    PQclear(res);

    /* Convert DB status to lowercase for consistency with the Go layer. */
    for (int i = 0; db_status[i]; i++)
        db_status[i] = (char)tolower((unsigned char)db_status[i]);

    return build_response_json(out, max_len, req, db_status, NULL);
}

int api_gateway_handle(const char* raw_json, char* resp_json, size_t max_len,
                       gateway_side_effect_t* side)
{
    if (!raw_json || !resp_json || max_len == 0)
        return -1;

    message_t req;
    if (deserialize_message_from_json(raw_json, &req) != 0)
        return -1;

    /* Extract command and keep payload around for command handlers. */
    cJSON* root = cJSON_Parse(raw_json);
    if (!root)
        return -1;

    cJSON* payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    cJSON* cmd_json = cJSON_GetObjectItemCaseSensitive(payload, "command");

    char cmd[CMD_BUF_SIZE] = {0};
    if (cJSON_IsString(cmd_json) && cmd_json->valuestring)
    {
        strncpy(cmd, cmd_json->valuestring, CMD_BUF_SIZE - 1);
        cmd[CMD_BUF_SIZE - 1] = '\0';
    }

    int rc;
    if (strcmp(cmd, "ping") == 0)
        rc = cmd_ping(resp_json, max_len, &req);
    else if (strcmp(cmd, "create_new_order") == 0)
        rc = cmd_create_shipment(resp_json, max_len, &req, payload, side);
    else if (strcmp(cmd, "get_shipment_status") == 0)
        rc = cmd_get_shipment_status(resp_json, max_len, &req, payload);
    else
        rc = build_error(resp_json, max_len, &req, "unknown command");

    cJSON_Delete(root);
    return rc;
}
