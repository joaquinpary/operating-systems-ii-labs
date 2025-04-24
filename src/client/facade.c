#include "facade.h"
#include "behavior.h"
#include "config.h"
#include "connection.h"
#include "inventory.h"
#include "logger.h"
#include "behavior_cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Father process for Sender
// Sender process for Receiver

int connection(init_params_client params)
{
    int authentication_status;
    connection_context context = {0};
    pid_t pid;

    if (!strcmp(params.connection_params.protocol, UDP))
    {
        context = init_connection_udp(params);
        authentication_status = authenticate(params, context);
        if (authentication_status != 0)
        {
            log_error("Authentication failed with ID: %s", get_identifiers()->client_id);
            close(context.sockfd);
            exit(EXIT_FAILURE);
        }
    }
    else if (!strcmp(params.connection_params.protocol, TCP))
    {
        context.sockfd = init_connection_tcp(params);
        authentication_status = authenticate(params, context);
        if (authentication_status != 0)
        {
            log_error("Authentication failed with ID: %s ", get_identifiers()->client_id);
            close(context.sockfd);
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        log_debug("Invalid protocol: %s", params.connection_params.protocol);
        exit(EXIT_FAILURE);
    }
    pid = fork();
    if (pid < 0)
    {
        log_debug("Fork failed");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0)
    {
        manager_receiver(params, context);
    }
    else
    {
        manager_sender(params, context);
    }
    return 0;
}

int connection_cli(init_params_client params)
{
    int authentication_status;
    int sockfd;
    connection_context context = {0};

    sockfd = init_connection_tcp(params);
    authentication_status = cli_authenticate(params, sockfd);
    if (authentication_status)
        return 1;
    if (logic_cli_sender_recv(params, sockfd))
        return 1;
    close(sockfd);
    log_info("Client finished with ID: %s", get_identifiers()->client_id);
    return 0;
}
