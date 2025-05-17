#define _GNU_SOURCE
#include "facade.h"
#include "behavior.h"
#include "behavior_cli.h"
#include "config.h"
#include "connection.h"
#include "inventory.h"
#include "logger.h"
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TIME 60

#define WAREHOUSE_REQUEST_STOCK 8
#define HUB_REQUEST_STOCK 10

// Father process for Sender
// Sender process for Receiver

volatile sig_atomic_t finish = 0;
pid_t child_pid = -1;

void handle_sigint(int sig)
{
    (void)sig;
    finish = 1;

    if (child_pid > 0)
    {
        kill(child_pid, SIGTERM);
    }
}

int connection(init_params_client params)
{
    int authentication_status;
    char* buffer = NULL;
    connection_context context = {0};

    signal(SIGINT, handle_sigint);
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
    authentication_status = authenticate(context);
    if (authentication_status != 0)
    {
        log_error("Authentication failed with ID: %s", get_identifiers()->username);
        close(context.sockfd);
        return 1;
    }

    buffer = receiver(context, params.connection_params.protocol);
    if (buffer == NULL)
    {
        log_error("Error receiving response with ID: %s", get_identifiers()->username);
        close(context.sockfd);
        return 1;
    }
    int* next_action = message_receiver(buffer, context);
    if (next_action == NULL)
    {
        log_error("Error processing response with ID: %s", get_identifiers()->username);
        free(buffer);
        close(context.sockfd);
        return 1;
    }
    log_error("Finished processing response with ID: %s", get_identifiers()->username);
    child_pid = fork();
    if (child_pid < 0)
    {
        log_error("Fork failed");
        close(context.sockfd);
        return 1;
    }
    else if (child_pid == 0)
    {
        signal(SIGINT, handle_sigint);
        exit(manager_receiver(context, &finish));
    }
    else
    {
        log_error("Finished in father processing response with ID: %s", get_identifiers()->username);
        if (manager_sender(context, TIME, &finish))
        {
            kill(child_pid, SIGTERM);
            waitpid(child_pid, NULL, 0);
            close(context.sockfd);
            return 1;
        }
        kill(child_pid, SIGTERM);
        waitpid(child_pid, NULL, 0);
        wait(NULL);
    }
    close(context.sockfd);
    log_info("FINISH CLIENT");
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
    log_info("Client finished with ID: %s", get_identifiers()->username);
    close(sockfd);
    return 0;
}
