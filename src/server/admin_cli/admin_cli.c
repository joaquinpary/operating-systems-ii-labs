/**
 * @file admin_cli.c
 * @brief Admin CLI plugin — loaded at runtime by the server via dlopen.
 *
 * Self-contained: owns its own libpq connection so adding new admin
 * commands never requires touching the server core (database.hpp, etc.).
 *
 * All list commands are paginated to fit within the 1024-byte TCP frame.
 */

#include "admin_cli_interface.h"
#include "cJSON.h"

#include <libpq-fe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADMIN_CLI_VERSION "1.0.0"
#define CLIENTS_PAGE_SIZE 10
#define TRANSACTIONS_PAGE_SIZE 3
#define CMD_TOKEN_BUF_SIZE 64

enum inv_col
{
    INV_CLIENT_TYPE,
    INV_FOOD,
    INV_WATER,
    INV_MEDICINE,
    INV_TOOLS,
    INV_GUNS,
    INV_AMMO,
    INV_LAST_UPDATED
};

enum txn_col
{
    TXN_ID,
    TXN_TYPE,
    TXN_SRC,
    TXN_SRC_TYPE,
    TXN_DST,
    TXN_DST_TYPE,
    TXN_STATUS,
    TXN_FOOD,
    TXN_WATER,
    TXN_MEDICINE,
    TXN_TOOLS,
    TXN_GUNS,
    TXN_AMMO
};

static PGconn* s_conn = NULL;

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

static int parse_int_arg(const char* args, const char* key, int default_val)
{
    if (!args || !key)
        return default_val;
    const char* p = strstr(args, key);
    if (!p)
        return default_val;
    p += strlen(key);
    while (*p == ' ')
        p++;
    if (*p == '\0')
        return default_val;
    int v = atoi(p);
    return v > 0 ? v : default_val;
}

/** Find "key VALUE" in args and return VALUE as a string (copied into buf).
 *  Returns 0 if found, -1 if not. */
static int parse_str_arg(const char* args, const char* key, char* buf, size_t buf_len)
{
    if (!args || !key)
        return -1;
    const char* p = strstr(args, key);
    if (!p)
        return -1;
    p += strlen(key);
    while (*p == ' ')
        p++;
    if (*p == '\0')
        return -1;
    size_t i = 0;
    while (*p && *p != ' ' && i < buf_len - 1)
        buf[i++] = *p++;
    buf[i] = '\0';
    return i > 0 ? 0 : -1;
}

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

static int build_paged(char* out, size_t max_len, const char* key, cJSON* data, int page, int total_pages, int total)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddNumberToObject(root, "page", page);
    cJSON_AddNumberToObject(root, "total_pages", total_pages);
    cJSON_AddNumberToObject(root, "total", total);
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

static int cmd_help(char* out, size_t max_len)
{
    cJSON* cmds = cJSON_CreateArray();

    const char* names[] = {"help", "clients", "inventory", "transactions"};
    const char* descs[] = {"Show available commands", "List clients (args: [active true|false] [page N])",
                           "Get inventory for a client (args: <client_id>)",
                           "List transactions (args: [<client_id>|all] [page N])"};

    for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); ++i)
    {
        cJSON* e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "command", names[i]);
        cJSON_AddStringToObject(e, "description", descs[i]);
        cJSON_AddItemToArray(cmds, e);
    }
    return build_ok(out, max_len, "commands", cmds);
}

static int cmd_clients(const char* args, char* out, size_t max_len)
{
    if (!s_conn)
        return build_error(out, max_len, "no database connection");

    int filter_active = 1;
    char active_val[8] = {0};
    if (parse_str_arg(args, "active ", active_val, sizeof(active_val)) == 0)
    {
        if (strcmp(active_val, "false") == 0 || strcmp(active_val, "0") == 0)
            filter_active = 0;
    }

    int page = parse_int_arg(args, "page ", 1);

    const char* active_str = filter_active ? "true" : "false";
    const char* count_params[1] = {active_str};
    PGresult* cnt_res = PQexecParams(s_conn, "SELECT COUNT(*) FROM credentials WHERE is_active = $1", 1, NULL,
                                     count_params, NULL, NULL, 0);

    if (PQresultStatus(cnt_res) != PGRES_TUPLES_OK)
    {
        PQclear(cnt_res);
        return build_error(out, max_len, "query failed");
    }

    int total = atoi(PQgetvalue(cnt_res, 0, 0));
    PQclear(cnt_res);

    int total_pages = (total + CLIENTS_PAGE_SIZE - 1) / CLIENTS_PAGE_SIZE;
    if (total_pages == 0)
        total_pages = 1;
    if (page > total_pages)
        page = total_pages;

    int offset = (page - 1) * CLIENTS_PAGE_SIZE;
    char limit_str[16], offset_str[16];
    snprintf(limit_str, sizeof(limit_str), "%d", CLIENTS_PAGE_SIZE);
    snprintf(offset_str, sizeof(offset_str), "%d", offset);

    const char* params[3] = {active_str, limit_str, offset_str};
    PGresult* res = PQexecParams(s_conn,
                                 "SELECT username, client_type, is_active "
                                 "FROM credentials WHERE is_active = $1 "
                                 "ORDER BY username LIMIT $2 OFFSET $3",
                                 3, NULL, params, NULL, NULL, 0);

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
        cJSON_AddStringToObject(obj, "type", PQgetvalue(res, i, 1));
        cJSON_AddItemToArray(arr, obj);
    }
    PQclear(res);
    return build_paged(out, max_len, "clients", arr, page, total_pages, total);
}

static int cmd_inventory(const char* client_id, char* out, size_t max_len)
{
    if (!s_conn)
        return build_error(out, max_len, "no database connection");
    if (!client_id || client_id[0] == '\0')
        return build_error(out, max_len, "inventory requires args: client_id");

    const char* params[1] = {client_id};
    PGresult* res = PQexecParams(s_conn,
                                 "SELECT client_type, food, water, medicine, tools, guns, ammo, last_updated "
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
    cJSON_AddStringToObject(obj, "client_type", PQgetvalue(res, 0, INV_CLIENT_TYPE));
    cJSON_AddNumberToObject(obj, "food", atoi(PQgetvalue(res, 0, INV_FOOD)));
    cJSON_AddNumberToObject(obj, "water", atoi(PQgetvalue(res, 0, INV_WATER)));
    cJSON_AddNumberToObject(obj, "medicine", atoi(PQgetvalue(res, 0, INV_MEDICINE)));
    cJSON_AddNumberToObject(obj, "tools", atoi(PQgetvalue(res, 0, INV_TOOLS)));
    cJSON_AddNumberToObject(obj, "guns", atoi(PQgetvalue(res, 0, INV_GUNS)));
    cJSON_AddNumberToObject(obj, "ammo", atoi(PQgetvalue(res, 0, INV_AMMO)));
    cJSON_AddStringToObject(obj, "last_updated", PQgetvalue(res, 0, INV_LAST_UPDATED));

    PQclear(res);
    return build_ok(out, max_len, "inventory", obj);
}

static int cmd_transactions(const char* args, char* out, size_t max_len)
{
    if (!s_conn)
        return build_error(out, max_len, "no database connection");

    int page = parse_int_arg(args, "page ", 1);

    char client_filter[CMD_TOKEN_BUF_SIZE] = {0};
    int has_filter = 0;

    if (args && args[0] != '\0')
    {
        char first[CMD_TOKEN_BUF_SIZE] = {0};
        int i = 0;
        const char* p = args;
        while (*p == ' ')
            p++;
        while (*p && *p != ' ' && i < CMD_TOKEN_BUF_SIZE - 1)
            first[i++] = *p++;
        first[i] = '\0';

        if (strcmp(first, "all") != 0 && strcmp(first, "page") != 0 && first[0] != '\0')
        {
            strncpy(client_filter, first, sizeof(client_filter) - 1);
            has_filter = 1;
        }
    }

    PGresult* cnt_res;
    if (has_filter)
    {
        const char* params[2] = {client_filter, client_filter};
        cnt_res = PQexecParams(s_conn,
                               "SELECT COUNT(*) FROM inventory_transactions "
                               "WHERE source_id = $1 OR destination_id = $2",
                               2, NULL, params, NULL, NULL, 0);
    }
    else
    {
        cnt_res = PQexec(s_conn, "SELECT COUNT(*) FROM inventory_transactions");
    }

    if (PQresultStatus(cnt_res) != PGRES_TUPLES_OK)
    {
        PQclear(cnt_res);
        return build_error(out, max_len, "query failed");
    }

    int total = atoi(PQgetvalue(cnt_res, 0, 0));
    PQclear(cnt_res);

    int total_pages = (total + TRANSACTIONS_PAGE_SIZE - 1) / TRANSACTIONS_PAGE_SIZE;
    if (total_pages == 0)
        total_pages = 1;
    if (page > total_pages)
        page = total_pages;

    int offset = (page - 1) * TRANSACTIONS_PAGE_SIZE;
    char limit_str[16], offset_str[16];
    snprintf(limit_str, sizeof(limit_str), "%d", TRANSACTIONS_PAGE_SIZE);
    snprintf(offset_str, sizeof(offset_str), "%d", offset);

    PGresult* res;
    if (has_filter)
    {
        const char* params[4] = {client_filter, client_filter, limit_str, offset_str};
        res = PQexecParams(s_conn,
                           "SELECT transaction_id, transaction_type, source_id, source_type, "
                           "destination_id, destination_type, status, food, water, medicine, "
                           "tools, guns, ammo FROM inventory_transactions "
                           "WHERE source_id = $1 OR destination_id = $2 "
                           "ORDER BY transaction_id DESC LIMIT $3 OFFSET $4",
                           4, NULL, params, NULL, NULL, 0);
    }
    else
    {
        const char* params[2] = {limit_str, offset_str};
        res = PQexecParams(s_conn,
                           "SELECT transaction_id, transaction_type, source_id, source_type, "
                           "destination_id, destination_type, status, food, water, medicine, "
                           "tools, guns, ammo FROM inventory_transactions "
                           "ORDER BY transaction_id DESC LIMIT $1 OFFSET $2",
                           2, NULL, params, NULL, NULL, 0);
    }

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
        cJSON_AddNumberToObject(obj, "id", atoi(PQgetvalue(res, i, TXN_ID)));
        cJSON_AddStringToObject(obj, "type", PQgetvalue(res, i, TXN_TYPE));
        cJSON_AddStringToObject(obj, "src", PQgetvalue(res, i, TXN_SRC));
        cJSON_AddStringToObject(obj, "dst", PQgetvalue(res, i, TXN_DST));
        cJSON_AddStringToObject(obj, "status", PQgetvalue(res, i, TXN_STATUS));
        cJSON_AddNumberToObject(obj, "food", atoi(PQgetvalue(res, i, TXN_FOOD)));
        cJSON_AddNumberToObject(obj, "water", atoi(PQgetvalue(res, i, TXN_WATER)));
        cJSON_AddNumberToObject(obj, "medicine", atoi(PQgetvalue(res, i, TXN_MEDICINE)));
        cJSON_AddNumberToObject(obj, "tools", atoi(PQgetvalue(res, i, TXN_TOOLS)));
        cJSON_AddNumberToObject(obj, "guns", atoi(PQgetvalue(res, i, TXN_GUNS)));
        cJSON_AddNumberToObject(obj, "ammo", atoi(PQgetvalue(res, i, TXN_AMMO)));
        cJSON_AddItemToArray(arr, obj);
    }
    PQclear(res);
    return build_paged(out, max_len, "transactions", arr, page, total_pages, total);
}

int admin_cli_handle(const char* raw_json, char* resp_json, size_t max_len)
{
    if (!raw_json || !resp_json || max_len == 0)
        return -1;

    cJSON* root = cJSON_Parse(raw_json);
    if (!root)
        return build_error(resp_json, max_len, "invalid JSON");

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
        rc = cmd_clients(args, resp_json, max_len);
    else if (strcmp(command, "inventory") == 0)
        rc = cmd_inventory(args, resp_json, max_len);
    else if (strcmp(command, "transactions") == 0)
        rc = cmd_transactions(args, resp_json, max_len);
    else
        rc = build_error(resp_json, max_len, "unknown command");

    cJSON_Delete(root);
    return rc;
}
