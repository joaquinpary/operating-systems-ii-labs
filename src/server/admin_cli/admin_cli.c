/**
 * @file admin_cli.c
 * @brief Admin CLI plugin — loaded at runtime by the server via dlopen.
 *
 * Self-contained: owns its own libpq connection so adding new admin
 * commands never requires touching the server core (database.hpp, etc.).
 */

#include "admin_cli_interface.h"
#include "cJSON.h"

#include <libpq-fe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADMIN_CLI_VERSION "1.0.0"

/* ---- internal state ----------------------------------------------------- */
static PGconn* s_conn = NULL;

/* ===== exported symbols ================================================== */

const char* admin_cli_version(void)
{
    return ADMIN_CLI_VERSION;
}

int admin_cli_init(const char* conn_string)
{
    if (!conn_string)
        return -1;

    s_conn = PQconnectdb(conn_string);
    if (PQstatus(s_conn) != CONNECTION_OK)
    {
        fprintf(stderr, "[admin_cli] PQconnectdb failed: %s\n", PQerrorMessage(s_conn));
        PQfinish(s_conn);
        s_conn = NULL;
        return -1;
    }
    return 0;
}

void admin_cli_shutdown(void)
{
    if (s_conn)
    {
        PQfinish(s_conn);
        s_conn = NULL;
    }
}

/* ===== JSON helpers ====================================================== */

static int build_error(char* out, size_t max_len, const char* message)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddStringToObject(root, "message", message);
    char* str = cJSON_PrintUnformatted(root);
    if (str)
    {
        snprintf(out, max_len, "%s", str);
        free(str);
    }
    cJSON_Delete(root);
    return -1;
}

static int build_ok(char* out, size_t max_len, const char* key, cJSON* data)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddItemToObject(root, key, data);
    char* str = cJSON_PrintUnformatted(root);
    if (str)
    {
        snprintf(out, max_len, "%s", str);
        free(str);
    }
    cJSON_Delete(root);
    return 0;
}

/* ===== command handlers ================================================== */

static int cmd_help(char* out, size_t max_len)
{
    cJSON* cmds = cJSON_CreateArray();

    const char* names[] = {"help", "clients", "inventory", "transactions"};
    const char* descs[] = {"Show available commands", "List active clients",
                           "Get inventory for a client (args: client_id)",
                           "List pending transactions (args: limit)"};

    for (int i = 0; i < 4; ++i)
    {
        cJSON* e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "command", names[i]);
        cJSON_AddStringToObject(e, "description", descs[i]);
        cJSON_AddItemToArray(cmds, e);
    }
    return build_ok(out, max_len, "commands", cmds);
}

static int cmd_clients(char* out, size_t max_len)
{
    if (!s_conn)
        return build_error(out, max_len, "no database connection");

    PGresult* res = PQexec(s_conn,
                           "SELECT username, client_type, is_active "
                           "FROM credentials WHERE is_active = true ORDER BY username");

    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return build_error(out, max_len, "query failed");
    }

    int rows = PQntuples(res);
    cJSON* arr = cJSON_CreateArray();
    for (int i = 0; i < rows; ++i)
    {
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "username", PQgetvalue(res, i, 0));
        cJSON_AddStringToObject(obj, "client_type", PQgetvalue(res, i, 1));
        cJSON_AddBoolToObject(obj, "is_active", PQgetvalue(res, i, 2)[0] == 't');
        cJSON_AddItemToArray(arr, obj);
    }
    PQclear(res);
    return build_ok(out, max_len, "clients", arr);
}

static int cmd_inventory(const char* client_id, char* out, size_t max_len)
{
    if (!s_conn)
        return build_error(out, max_len, "no database connection");
    if (!client_id || client_id[0] == '\0')
        return build_error(out, max_len, "inventory requires args: client_id");

    const char* params[1] = {client_id};
    PGresult* res = PQexecParams(s_conn,
                                 "SELECT client_type, food, water, medicine, tools, guns, ammo, last_update "
                                 "FROM client_inventory WHERE client_id = $1",
                                 1, NULL, params, NULL, NULL, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return build_error(out, max_len, "query failed");
    }

    if (PQntuples(res) == 0)
    {
        PQclear(res);
        return build_error(out, max_len, "client not found");
    }

    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "client_id", client_id);
    cJSON_AddStringToObject(obj, "client_type", PQgetvalue(res, 0, 0));
    cJSON_AddNumberToObject(obj, "food", atoi(PQgetvalue(res, 0, 1)));
    cJSON_AddNumberToObject(obj, "water", atoi(PQgetvalue(res, 0, 2)));
    cJSON_AddNumberToObject(obj, "medicine", atoi(PQgetvalue(res, 0, 3)));
    cJSON_AddNumberToObject(obj, "tools", atoi(PQgetvalue(res, 0, 4)));
    cJSON_AddNumberToObject(obj, "guns", atoi(PQgetvalue(res, 0, 5)));
    cJSON_AddNumberToObject(obj, "ammo", atoi(PQgetvalue(res, 0, 6)));
    cJSON_AddStringToObject(obj, "last_update", PQgetvalue(res, 0, 7));

    PQclear(res);
    return build_ok(out, max_len, "inventory", obj);
}

static int cmd_transactions(int limit, char* out, size_t max_len)
{
    if (!s_conn)
        return build_error(out, max_len, "no database connection");

    char limit_str[16];
    snprintf(limit_str, sizeof(limit_str), "%d", limit > 0 ? limit : 50);

    const char* params[1] = {limit_str};
    PGresult* res = PQexecParams(s_conn,
                                 "SELECT transaction_id, transaction_type, source_id, source_type, "
                                 "destination_id, destination_type, status, food, water, medicine, "
                                 "tools, guns, ammo FROM inventory_transactions "
                                 "ORDER BY transaction_id DESC LIMIT $1",
                                 1, NULL, params, NULL, NULL, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return build_error(out, max_len, "query failed");
    }

    int rows = PQntuples(res);
    cJSON* arr = cJSON_CreateArray();
    for (int i = 0; i < rows; ++i)
    {
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "id", atoi(PQgetvalue(res, i, 0)));
        cJSON_AddStringToObject(obj, "type", PQgetvalue(res, i, 1));
        cJSON_AddStringToObject(obj, "source_id", PQgetvalue(res, i, 2));
        cJSON_AddStringToObject(obj, "source_type", PQgetvalue(res, i, 3));
        cJSON_AddStringToObject(obj, "destination_id", PQgetvalue(res, i, 4));
        cJSON_AddStringToObject(obj, "destination_type", PQgetvalue(res, i, 5));
        cJSON_AddStringToObject(obj, "status", PQgetvalue(res, i, 6));
        cJSON_AddNumberToObject(obj, "food", atoi(PQgetvalue(res, i, 7)));
        cJSON_AddNumberToObject(obj, "water", atoi(PQgetvalue(res, i, 8)));
        cJSON_AddNumberToObject(obj, "medicine", atoi(PQgetvalue(res, i, 9)));
        cJSON_AddNumberToObject(obj, "tools", atoi(PQgetvalue(res, i, 10)));
        cJSON_AddNumberToObject(obj, "guns", atoi(PQgetvalue(res, i, 11)));
        cJSON_AddNumberToObject(obj, "ammo", atoi(PQgetvalue(res, i, 12)));
        cJSON_AddItemToArray(arr, obj);
    }
    PQclear(res);
    return build_ok(out, max_len, "transactions", arr);
}

/* ===== main dispatch ===================================================== */

int admin_cli_handle(const char* raw_json, char* resp_json, size_t max_len)
{
    if (!raw_json || !resp_json || max_len == 0)
        return -1;

    cJSON* root = cJSON_Parse(raw_json);
    if (!root)
        return build_error(resp_json, max_len, "invalid JSON");

    /* Extract payload.command and payload.args */
    const char* command = NULL;
    const char* args = NULL;

    cJSON* payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (payload && cJSON_IsObject(payload))
    {
        cJSON* cmd = cJSON_GetObjectItemCaseSensitive(payload, "command");
        if (cJSON_IsString(cmd))
            command = cmd->valuestring;
        cJSON* arg = cJSON_GetObjectItemCaseSensitive(payload, "args");
        if (cJSON_IsString(arg))
            args = arg->valuestring;
    }

    if (!command)
    {
        cJSON_Delete(root);
        return build_error(resp_json, max_len, "missing payload.command");
    }

    int rc;

    if (strcmp(command, "help") == 0)
        rc = cmd_help(resp_json, max_len);
    else if (strcmp(command, "clients") == 0)
        rc = cmd_clients(resp_json, max_len);
    else if (strcmp(command, "inventory") == 0)
        rc = cmd_inventory(args, resp_json, max_len);
    else if (strcmp(command, "transactions") == 0)
    {
        int limit = 50;
        if (args && args[0] != '\0')
        {
            int parsed = atoi(args);
            if (parsed > 0)
                limit = parsed;
        }
        rc = cmd_transactions(limit, resp_json, max_len);
    }
    else
        rc = build_error(resp_json, max_len, "unknown command");

    cJSON_Delete(root);
    return rc;
}
