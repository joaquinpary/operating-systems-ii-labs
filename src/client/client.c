#include "client.h"
#include "connection.h"
#include "logger.h"
#include "logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_ATTEMPTS 3
#define LINE_SIZE 512

static int parse_conf(const char* path, client_config* config, client_credentials* creds)
{
    FILE* f = fopen(path, "r");
    if (!f)
    {
        LOG_ERROR_MSG("Failed to open config file '%s': %s", path, strerror(errno));
        return -1;
    }
    char line[LINE_SIZE];

    memset(config, 0, sizeof(*config));
    memset(creds, 0, sizeof(*creds));

    while (fgets(line, sizeof(line), f))
    {
        char* eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;

        while (*key == ' ' || *key == '\t')
            key++;
        char* key_end = eq - 1;
        while (key_end > key && (*key_end == ' ' || *key_end == '\t'))
        {
            *key_end = '\0';
            key_end--;
        }

        while (*val == ' ' || *val == '\t')
            val++;
        for (char* p = val + strlen(val) - 1; p >= val && (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t'); --p)
            *p = '\0';

        if (strcasecmp(key, "host") == 0)
            strncpy(config->host, val, sizeof(config->host) - 1);
        else if (strcasecmp(key, "port") == 0)
            strncpy(config->port, val, sizeof(config->port) - 1);
        else if (strcasecmp(key, "protocol") == 0)
            config->protocol = (strcasecmp(val, "tcp") == 0) ? PROTO_TCP : PROTO_UDP;
        else if (strcasecmp(key, "ipversion") == 0)
        {
            if (strcasecmp(val, "v6") == 0 || strcasecmp(val, "ipv6") == 0)
                config->ip_version = AF_INET6;
            else if (strcasecmp(val, "v4") == 0 || strcasecmp(val, "ipv4") == 0)
                config->ip_version = AF_INET;
            else
                config->ip_version = AF_UNSPEC;
        }
        else if (strcasecmp(key, "type") == 0)
            strncpy(creds->type, val, sizeof(creds->type) - 1);
        else if (strcasecmp(key, "username") == 0)
            strncpy(creds->username, val, sizeof(creds->username) - 1);
        else if (strcasecmp(key, "password") == 0)
            strncpy(creds->password, val, sizeof(creds->password) - 1);
    }
    fclose(f);

    if (config->host[0] == '\0' || config->port[0] == '\0' || creds->username[0] == '\0' || creds->password[0] == '\0')
    {
        LOG_ERROR_MSG("Missing required fields in config file '%s'", path);
        return -1;
    }

    LOG_INFO_MSG("Configuration loaded successfully from '%s'", path);
    return 0;
}

int authenticate(client_context* ctx, client_credentials* creds)
{
    message_t msg;
    char json_buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    LOG_INFO_MSG("Starting authentication for user '%s'", creds->username);

    for (int i = 0; i < MAX_ATTEMPTS; i++)
    {
        if (create_auth_request_message(&msg, creds->type, creds->username, creds->username, creds->password) != 0)
        {
            LOG_ERROR_MSG("Failed to create auth request message");
            return -1;
        }

        if (serialize_message_to_json(&msg, json_buffer) != 0)
        {
            LOG_ERROR_MSG("Failed to serialize auth request message");
            return -1;
        }

        if (client_send(ctx, json_buffer) != 0)
        {
            LOG_ERROR_MSG("Failed to send auth request message (attempt %d/%d)", i + 1, MAX_ATTEMPTS);
            continue;
        }

        int bytes = client_receive(ctx, response, BUFFER_SIZE);
        if (bytes <= 0)
        {
            LOG_ERROR_MSG("Failed to receive auth response message (attempt %d/%d)", i + 1, MAX_ATTEMPTS);
            continue;
        }

        message_t response_msg;
        if (deserialize_message_from_json(response, &response_msg) != 0)
        {
            LOG_ERROR_MSG("Failed to deserialize auth response");
            continue;
        }

        if (strcmp(response_msg.msg_type, SERVER_TO_HUB__AUTH_RESPONSE) == 0 ||
            strcmp(response_msg.msg_type, SERVER_TO_WAREHOUSE__AUTH_RESPONSE) == 0)
        {
            if (response_msg.payload.server_auth_response.status_code == 200)
            {
                message_t ack_msg;
                if (create_acknowledgment_message(&ack_msg, creds->type, creds->username, SERVER, SERVER,
                                                  response_msg.timestamp, 200) == 0)
                {
                    if (serialize_message_to_json(&ack_msg, json_buffer) == 0)
                    {
                        if (client_send(ctx, json_buffer) == 0)
                        {
                            LOG_INFO_MSG("Sent ACK for successful authentication");
                        }
                        else
                        {
                            LOG_WARNING_MSG("Failed to send ACK message");
                        }
                    }
                }
                else
                {
                    LOG_ERROR_MSG("Failed to create ACK message");
                    return -1;
                }
                LOG_INFO_MSG("Authentication successful");
                printf("[AUTH] Authentication successful\n");
                return 0;
            }
            else
            {
                LOG_ERROR_MSG("Authentication failed with status code %d",
                              response_msg.payload.server_auth_response.status_code);
                return -1;
            }
        }
        LOG_WARNING_MSG("Unexpected response type: %s", response_msg.msg_type);
    }
    LOG_ERROR_MSG("Authentication failed after %d attempts", MAX_ATTEMPTS);
    return -1;
}

int run_client(const char* config_path)
{
    client_config config;
    client_context ctx;
    client_credentials creds;

    if (parse_conf(config_path, &config, &creds) != 0)
    {
        fprintf(stderr, "Failed to parse config file '%s'\n", config_path);
        return 1;
    }

    // Initialize logger with per-client log file
    const char* log_dir = getenv("LOG_DIR");
    if (!log_dir)
        log_dir = "/tmp";

    char log_path[FILE_PATH];
    snprintf(log_path, sizeof(log_path), "%s/%s.log", log_dir, creds.username);

    logger_config_t log_config = {.max_file_size = 10 * 1024 * 1024, // 10 MB
                                  .max_backup_files = 5,
                                  .min_level = LOG_DEBUG};
    
    strncpy(log_config.log_file_path, log_path, FILE_PATH - 1);
    log_config.log_file_path[FILE_PATH - 1] = '\0';

    if (log_init(&log_config) != 0)
    {
        fprintf(stderr, "Failed to initialize logger\n");
        return 1;
    }

    LOG_INFO_MSG("=== DHL Client Starting ===");
    LOG_INFO_MSG("Configuration loaded from: %s", config_path);

    printf("Initializing client from %s (%s:%s %s) ...\n", config_path, config.host, config.port,
           (config.protocol == PROTO_TCP ? "tcp" : "udp"));

    LOG_INFO_MSG("Connecting to %s:%s (%s)", config.host, config.port, config.protocol == PROTO_TCP ? "TCP" : "UDP");

    if (client_init(&ctx, &config) == 0)
    {
        LOG_INFO_MSG("Connection established successfully");

        if (authenticate(&ctx, &creds) != 0)
        {
            printf("Authentication failed.\n");
            LOG_ERROR_MSG("Authentication failed, closing connection");
            client_close(&ctx);
            log_close();
            return -1;
        }

        printf("Connection successful.\n");
        LOG_INFO_MSG("Starting client logic");

        if (logic_init(&ctx, creds.type, creds.username) != 0)
        {
            printf("Client logic encountered an error.\n");
            LOG_ERROR_MSG("Client logic failed");
            client_close(&ctx);
            log_close();
            return -1;
        }

        LOG_INFO_MSG("Client shutting down normally");
        client_close(&ctx);
        log_close();
        return 0;
    }

    LOG_ERROR_MSG("Failed to initialize client connection");
    log_close();
    return 1;
}
