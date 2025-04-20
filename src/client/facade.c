#include "facade.h"
#include "behavior.h"
#include "config.h"
#include "connection.h"
#include "inventory.h"
#include "logger.h"
#include <stdio.h>

// Father process for Sender
// Sender process for Receiver

int connection(init_params_client params)
{
    int authentication_status;
    connection_context context = {0};
    pid_t pid;

    if (!strcmp(params.protocol, UDP))
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
    else if (!strcmp(params.protocol, TCP))
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
        log_debug("Invalid protocol: %s", params.protocol);
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

int init_connection_cli()
{
}
