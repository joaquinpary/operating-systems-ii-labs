#include "client.h"
#include "connection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_ATTEMPTS 3

static int parse_conf(const char* path, client_config* config, client_credentials* creds)
{
    FILE* f = fopen(path, "r");
    if (!f)
    {
        perror("fopen");
        return -1;
    }
    char line[512];

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
        fprintf(stderr, "[CONFIG] missing required fields in %s\n", path);
        return -1;
    }
    return 0;
}

int authenticate(client_context* ctx, client_credentials* creds)
{
    message_t msg;
    char json_buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    for (int i = 0; i < MAX_ATTEMPTS; i++)
    {
        if (create_auth_request_message(&msg, creds->type, creds->username, creds->username, creds->password) != 0)
        {
            fprintf(stderr, "[AUTH] Failed to create auth request message\n");
            return -1;
        }

        if (serialize_message_to_json(&msg, json_buffer) != 0)
        {
            fprintf(stderr, "[AUTH] Failed to serialize auth request message\n");
            return -1;
        }

        if (client_send(ctx, json_buffer) != 0)
        {
            fprintf(stderr, "[AUTH] Failed to send auth request message\n");
            return -1;
        }

        int bytes = client_receive(ctx, response, BUFFER_SIZE);
        if (bytes <= 0)
        {
            fprintf(stderr, "[AUTH] Failed to receive auth response message\n");
            return -1;
        }

        message_t response_msg;
        if (deserialize_message_from_json(response, &response_msg) != 0)
        {
            fprintf(stderr, "[AUTH] Failed to deserialize auth response\n");
            return -1;
        }

        if (strcmp(response_msg.msg_type, SERVER_TO_HUB__AUTH_RESPONSE) == 0 ||
            strcmp(response_msg.msg_type, SERVER_TO_WAREHOUSE__AUTH_RESPONSE) == 0)
        {
            if (response_msg.payload.server_auth_response.status_code == 200)
            {
                printf("[AUTH] Authentication successful\n");
                return 0;
            }
            else
            {
                fprintf(stderr, "[AUTH] Authentication failed with status code %d\n",
                        response_msg.payload.server_auth_response.status_code);
                return -1;
            }
        }
        fprintf(stderr, "[AUTH] Unexpected response type: %s\n", response_msg.msg_type);
    }
    fprintf(stderr, "[AUTH] Authentication failed after %d attempts.\n", MAX_ATTEMPTS);
    return -1;
}

int run_client(const char* config_path)
{
    client_config config;
    client_context ctx;
    client_credentials creds;
    char response[BUFFER_SIZE];

    if (parse_conf(config_path, &config, &creds) != 0)
        return 1;

    printf("Initializing client from %s (%s:%s %s) ...\n", config_path, config.host, config.port,
           (config.protocol == PROTO_TCP ? "tcp" : "udp"));

    if (client_init(&ctx, &config) == 0)
    {
        if (authenticate(&ctx, &creds) != 0)
        {
            printf("Authentication failed.\n");
            client_close(&ctx);
            return -1;
        }
        printf("Connection successful.\n");
        client_close(&ctx);
        return 0;
    }
    return 1;
}
