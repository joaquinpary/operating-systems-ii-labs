#define _POSIX_C_SOURCE 200809L
#include "behavior_cli.h"
#include "config.h"
#include "connection.h"
#include "json_manager.h"
#include "logger.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define SUCCESS "success"
#define FAILURE "failure"

#define AUTH_REQUEST 0
#define AUTH_RESPONSE "auth_response"
#define END_OF_MESSAGE "end_of_message"
#define TRANSACTION_HISTORY "server_transactions_history"
#define CLIENT_LIVES "all_clients_live"
#define CLI_TRANSACTION_HISTORY 1
#define CLI_ALL_CLIENT_LIVES 2
#define CLI_EXIT 3
#define ITEM_TYPE 6

int cli_authenticate(init_params_client params, int sockfd)
{
    char* response = malloc(SOCKET_SIZE);
    if (!response)
    {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }
    if (cli_message_sender(params, sockfd, AUTH_REQUEST))
    {
        free(response);
        return 1;
    }
    if (receiver_tcp(sockfd, response))
    {
        free(response);
        return 1;
    }
    if (validate_checksum(response))
    {
        log_error("Checksum error with ID: %s", get_identifiers()->username);
        free(response);
        return 1;
    }
    server_auth_response auth_res = deserialize_server_auth_response(response);
    if (!strcmp(auth_res.payload.status, SUCCESS))
    {
        log_info("Authentication successful with ID: %s", get_identifiers()->username);
        printf("Authentication successful.\n");
        set_session_token(auth_res.payload.session_token);
        free(response);
        return 0;
    }
    else if (!strcmp(auth_res.payload.status, FAILURE))
    {
    }
    free(response);
    return 1;
}

int decode_transaction_history(init_params_client params, int sockfd)
{
    char* type = NULL;
    char* buffer = malloc(SOCKET_SIZE);
    if (!buffer)
    {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }
    print_transaction_table_header();
    while (1)
    {
        memset(buffer, 0, SOCKET_SIZE);
        if (receiver_tcp(sockfd, buffer))
        {
            free(buffer);
            return 1;
        }
        if (validate_checksum(buffer))
        {
            log_error("Checksum error with ID: %s", get_identifiers()->username);
            free(buffer);
            return 1;
        }
        type = get_type(buffer);
        if (!strcmp(type, END_OF_MESSAGE))
        {
            free(type);
            break;
        }
        server_transaction_history transaction = deserialize_server_transaction_history(buffer);
        print_transaction_row(&transaction);
        free(type);
    }
    free(buffer);
    return 0;
}

int decode_client_lives(init_params_client params, int sockfd)
{
    char* type = NULL;
    char* buffer = malloc(SOCKET_SIZE);
    if (!buffer)
    {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }
    print_client_table_header();
    while (1)
    {
        memset(buffer, 0, SOCKET_SIZE);
        if (receiver_tcp(sockfd, buffer))
        {
            free(buffer);
            return 1;
        }
        if (validate_checksum(buffer))
        {
            log_error("Checksum error with ID: %s", get_identifiers()->username);
            free(buffer);
            return 1;
        }
        type = get_type(buffer);
        if (!strcmp(type, END_OF_MESSAGE))
        {
            free(type);
            break;
        }
        server_client_alive client = deserialize_server_client_alive(buffer);
        print_client_row(&client);
        free(type);
    }
    free(buffer);
    return 0;
}

int cli_message_sender(init_params_client params, int sockfd, int type_message)
{
    char* buffer = NULL;
    switch (type_message)
    {
    case AUTH_REQUEST:
        client_auth_request auth_req = create_client_auth_request(params.client_type, params.username, params.password);
        buffer = serialize_client_auth_request(&auth_req);
        if (buffer == NULL)
            return 1;
        if (sender_tpc(sockfd, buffer))
        {
            free(buffer);
            return 1;
        }
        free(buffer);
        break;
    default:
        cli_message msg = create_cli_message(params.username, get_identifiers()->session_token, type_message);
        buffer = serialize_cli_message(&msg);
        if (buffer == NULL)
            return 1;
        if (sender_tpc(sockfd, buffer))
        {
            free(buffer);
            return 1;
        }
        free(buffer);
        if (type_message == CLI_EXIT)
        {
            printf("Exiting...\n");
            close(sockfd);
            return 0;
        }
        break;
    }
    return 0;
}

int cli_message_receiver(init_params_client params, int sockfd, int request_type)
{
    switch (request_type)
    {
    case CLI_TRANSACTION_HISTORY:
        if (decode_transaction_history(params, sockfd))
            return 1;
        printf("Transaction history received.\n");
        break;
    case CLI_ALL_CLIENT_LIVES:
        if (decode_client_lives(params, sockfd))
            return 1;
        printf("All clients' lives received.\n");
        break;
    default:
        break;
    }
    return 0;
}

int logic_cli_sender_recv(init_params_client params, int sockfd)
{
    pid_t pid;
    int action;
    int authentication_status;
    char command[BUFFER_SIZE];
    char* response = malloc(SOCKET_SIZE);
    if (!response)
    {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }
    for (int i = 0; i < 3; i++)
    {
        printf("username: ");
        fgets(command, sizeof(command), stdin);
        sscanf(command, "%s", params.username);
        printf("password: ");
        fgets(command, sizeof(command), stdin);
        sscanf(command, "%s", params.password);
        authentication_status = cli_authenticate(params, sockfd);
        if (!authentication_status)
            break;
        printf("Authentication failed\n");
    }
    if (authentication_status)
    {
        printf("Exiting...\n");
        return 1;
    }
    while (1)
    {
        printf("OPTIONS:\n \t- 1: REQUEST TRANSACTIONS HISTORY\n \t- 2: REQUEST ALL CLIENT LIVES\n \t- 3: EXIT\n");
        printf("CLI> ");
        fgets(command, sizeof(command), stdin);
        action = atoi(command);
        if (action < 1 || action > 3)
        {
            printf("Invalid option. Please try again.\n");
            continue;
        }
        pid = fork();
        if (pid < 0)
        {
            fprintf(stderr, "Fork failed\n");
            exit(EXIT_FAILURE);
        }
        else if (pid == 0)
        {
            if (cli_message_receiver(params, sockfd, action))
                exit(EXIT_FAILURE);
            exit(EXIT_SUCCESS);
        }
        else
        {
            if (cli_message_sender(params, sockfd, action))
            {
                kill(pid, SIGTERM);
                wait(NULL);
                return 1;
            }
            wait(NULL);
            if (action == CLI_EXIT)
                break;
        }
    }
    return 0;
}

void print_transaction_table_header()
{
    printf("+------+-------------+-----------------+----------------------+----------------------+---------------------"
           "-+-------------------------------------------------------------+---------------+\n");
    printf("| ID   | Hub ID      | Warehouse ID    | Requested            | Dispatched           | Received            "
           " | Items                                                       | Status        |\n");
    printf("+------+-------------+-----------------+----------------------+----------------------+---------------------"
           "-+-------------------------------------------------------------+---------------+\n");
    return;
}

void print_client_table_header()
{
    printf("+------------+-----------+-----------+\n");
    printf("| Username   | Type      | Status    |\n");
    printf("+------------+-----------+-----------+\n");
    return;
}

void print_transaction_row(const server_transaction_history* t)
{
    printf("| %-4d | %-11d | %-15d | %-20s | %-20s | %-20s | ", t->id, t->hub_id, t->warehouse_id,
           t->timestamp_requested, t->timestamp_dispatched, t->timestamp_received);

    for (int i = 0; i < ITEM_TYPE; i++)
    {
        if (strlen(t->items[i].item) > 0)
        {
            printf("%s: %d", t->items[i].item, t->items[i].quantity);
            if (i < ITEM_TYPE - 1 && strlen(t->items[i + 1].item) > 0)
                printf(", ");
        }
    }

    printf(" | %-13s | %-12s |\n", t->origin, t->destination);
    return;
}

void print_client_row(const server_client_alive* c)
{
    printf("| %-10s | %-9s | %-9s |\n", c->username, c->client_type, c->status);
    return;
}
