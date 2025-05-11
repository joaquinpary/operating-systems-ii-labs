#include "dhl_client.h"
#include "config.h"
#include "facade.h"
#include "inventory.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TESTING
#define PATH_CONFIG "config/clients_credentials.json"
#define LOG_BASE_PATH "logs/clients.log"
#else
#define PATH_CONFIG "/etc/dhl_client/clients_credentials.json"
#define LOG_BASE_PATH "/var/log/dhl_client/client"
#endif

#define CLIENT "CLIENT"
#define ADMIN "admin"
#define IP "ipv4"
#define PROTOCOL "tcp"

int start_client(char* client)
{
    char log_path[256];
    init_params_client params = load_config_client(PATH_CONFIG, atoi(client));
    set_params(params);
    snprintf(log_path, sizeof(log_path), "%s_%s.log", LOG_BASE_PATH, params.client_id);
    log_init(log_path, CLIENT);
    set_log_level(LOG_LEVEL_DEBUG);
    log_info("Client started with ID: %s", get_identifiers()->client_id);
    init_shared_memory();
    if (connection(params))
        return 1;
    return 0;
}

int start_cli()
{
    init_params_client params = {0};
    char command[BUFFER_SIZE];
    char log_path[256];
    printf("Welcome to the CLI!\n");
    printf("Please enter: \n\t -host: \n\t -port:\n");
    printf("host: ");
    fgets(command, sizeof(command), stdin);
    sscanf(command, "%s", params.connection_params.host);
    printf("port: ");
    fgets(command, sizeof(command), stdin);
    sscanf(command, "%s", params.connection_params.port);
    strncpy(params.client_id, ADMIN, MIN_SIZE - 1);
    params.client_id[MIN_SIZE - 1] = '\0';
    strncpy(params.client_type, ADMIN, MIN_SIZE - 1);
    params.client_type[MIN_SIZE - 1] = '\0';
    strncpy(params.connection_params.protocol, PROTOCOL, MIN_SIZE - 1);
    params.connection_params.protocol[MIN_SIZE - 1] = '\0';
    strncpy(params.connection_params.ip_version, IP, MIN_SIZE - 1);
    params.connection_params.ip_version[MIN_SIZE - 1] = '\0';
    set_client_id(params.client_id);
    snprintf(log_path, sizeof(log_path), "%s_%s.log", LOG_BASE_PATH, params.client_id);
    log_init(log_path, CLIENT);
    set_log_level(LOG_LEVEL_DEBUG);
    if (connection_cli(params))
        return 1;
    return 0;
}
