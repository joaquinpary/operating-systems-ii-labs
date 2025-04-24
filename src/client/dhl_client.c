#include "dhl_client.h"
#include "config.h"
#include "facade.h"
#include "inventory.h"
#include "logger.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define PATH_LOG "logs/client.log"
#define CLIENT "CLIENT"
#define ADMIN "admin"
#define IP "ipv4"
#define PROTOCOL "tcp"

int start_client(char* client)
{
    init_params_client params = load_config_client(PATH_CONFIG, atoi(client));
    set_client_id(params.client_id);
    printf("Client: %s\n", get_identifiers()->client_id);
    printf("Client SHM path: %s\n", get_identifiers()->shm_path);
    log_init(PATH_LOG, CLIENT);
    set_log_level(LOG_LEVEL_DEBUG);
    log_info("Client started with ID: %s", get_identifiers()->client_id);
    init_inventory_random();
    connection(params);
    return 0;
}

int start_cli()
{
    init_params_client params = {0};
    char command[BUFFER_SIZE];
    printf("Welcome to the CLI!\n");
    printf("Please enter: \n\t- username:\n\t- password: \n\t -host: \n\t -port:\n");
    printf("username: ");
    fgets(command, sizeof(command), stdin);
    sscanf(command, "%s", params.username);
    printf("password: ");
    fgets(command, sizeof(command), stdin);
    sscanf(command, "%s", params.password);
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

    printf("client_type: %s\n", params.client_type);
    printf("client_id: %s\n", params.client_id);
    printf("username: %s\n", params.username);
    printf("password: %s\n", params.password);
    printf("host: %s\n", params.connection_params.host);
    printf("port: %s\n", params.connection_params.port);
    printf("protocol: %s\n", params.connection_params.protocol);
    printf("ip_version: %s\n", params.connection_params.ip_version);

    set_client_id(params.client_id);
    printf("Client: %s\n", get_identifiers()->client_id);
    printf("Client SHM path: %s\n", get_identifiers()->shm_path);
    log_init(PATH_LOG, CLIENT);
    set_log_level(LOG_LEVEL_DEBUG);
    if (connection_cli(params))
        return 1;

    return 0;
}
