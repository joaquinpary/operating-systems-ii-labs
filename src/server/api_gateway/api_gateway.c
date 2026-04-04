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

int api_gateway_handle(const char* raw_json, char* resp_json, size_t max_len)
{
    if (!raw_json || !resp_json || max_len == 0)
        return -1;

    message_t req;
    if (deserialize_message_from_json(raw_json, &req) != 0)
        return -1;

    /* Extract command from payload. */
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

    cJSON_Delete(root);

    if (strcmp(cmd, "ping") == 0)
        return cmd_ping(resp_json, max_len, &req);

    return build_error(resp_json, max_len, &req, "unknown command");
}
