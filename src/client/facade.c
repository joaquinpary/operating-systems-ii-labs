#include "facade.h"
#include "behavior.h"
#include "behavior_cli.h"
#include "config.h"
#include "connection.h"
#include "logger.h"
#include "shared_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>

#define TIME 60
#define FINISH 0
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
        if (context.sockfd < 0)
        {
            log_error("Error initializing UDP connection");
            return 1;
        }
    }
    else if (!strcmp(params.connection_params.protocol, TCP))
    {
        context.sockfd = init_connection_tcp(params);
        if (context.sockfd < 0)
        {
            log_error("Error initializing TCP connection");
            return 1;
        }
    }

    else
    {
        log_debug("Invalid protocol: %s", params.connection_params.protocol);
        return 1;
    }
    authentication_status = authenticate(params, context);
    if (authentication_status != 0)
    {
        log_error("Authentication failed with ID: %s", get_identifiers()->client_id);
        close(context.sockfd);
        return 1;
    }
    pid = fork();
    if (pid < 0)
    {
        log_debug("Fork failed");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0)
    {
        if (manager_receiver(params, context, FINISH))
            exit(EXIT_FAILURE);
    }
    else
    {
        if (manager_sender(params, context, TIME, FINISH))
        {
            wait(NULL);
            close(context.sockfd);
            return 1;
        }
        wait(NULL);
    }
    close(context.sockfd);
    return 0;
}

int connection_cli(init_params_client params)
{
    int sockfd;
    sockfd = init_connection_tcp(params);
    if (sockfd < 0)
    {
        log_error("Error initializing TCP connection");
        return 1;
    }
    if (logic_cli_sender_recv(params, sockfd))
        return 1;
    log_info("Client finished with ID: %s", get_identifiers()->client_id);
    close(sockfd);
    return 0;
}
