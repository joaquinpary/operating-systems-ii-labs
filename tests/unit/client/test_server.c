// test_server.c

#include "test_server.h"
#include "json_manager.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static void* handle_client(void* arg);

typedef struct
{
    test_server_t* server;
} server_thread_arg_t;

static void* server_thread_func(void* arg)
{
    server_thread_arg_t* thread_arg = (server_thread_arg_t*)arg;
    test_server_t* server = thread_arg->server;
    free(thread_arg);

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    char buffer[1024];

    if (server->config.mode == SERVER_MODE_TCP)
    {
        while (server->running)
        {
            int client_socket = accept(server->socket_fd, (struct sockaddr*)&addr, &addrlen);
            if (client_socket >= 0)
            {
                pthread_t client_thread;
                int* client_sock_ptr = malloc(sizeof(int));
                *client_sock_ptr = client_socket;
                pthread_create(&client_thread, NULL, handle_client, client_sock_ptr);
                pthread_detach(client_thread);
            }
        }
    }
    else if (server->config.mode == SERVER_MODE_UDP)
    {
        while (server->running)
        {
            memset(buffer, 0, sizeof(buffer));
            ssize_t bytes_read =
                recvfrom(server->socket_fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&addr, &addrlen);
            if (bytes_read > 0)
            {
                printf("Server received (UDP): %s\n", buffer);

                if (strcmp(buffer, "server_quit") == 0)
                {
                    printf("Server shutting down (UDP)...\n");
                    server->running = 0;
                    break;
                }

                char* type = get_type(buffer);
                if (type && !strcmp(type, "client_auth_request"))
                {
                    client_auth_request auth_req = deserialize_client_auth_request(buffer);
                    server_auth_response auth_res;
                    if (!strcmp(auth_req.payload.username, "user_test") &&
                        !strcmp(auth_req.payload.password, "pass_test"))
                        auth_res =
                            create_server_auth_response("success", "session_token_test", "Authentication successful");
                    else
                        auth_res = create_server_auth_response("failure", "", "Authentication failed");

                    char* serialized_response = serialize_server_auth_response(&auth_res);
                    sendto(server->socket_fd, serialized_response, strlen(serialized_response), 0,
                           (struct sockaddr*)&addr, addrlen);
                    free(serialized_response);
                }
                else if (strcmp(buffer, "HELLO") == 0)
                {
                    sendto(server->socket_fd, "WORLD", strlen("WORLD"), 0, (struct sockaddr*)&addr, addrlen);
                }
                else
                {
                    sendto(server->socket_fd, "UNKNOWN", strlen("UNKNOWN"), 0, (struct sockaddr*)&addr, addrlen);
                }
            }
        }
    }

    return NULL;
}

static void* handle_client(void* arg)
{
    int client_socket = *(int*)arg;
    free(arg);

    char buffer[1024];

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0)
            break;

        printf("Server received (TCP): %s\n", buffer);

        if (strcmp(buffer, "server_quit") == 0)
        {
            break;
        }

        char* type = get_type(buffer);
        if (type && !strcmp(type, "client_auth_request"))
        {
            client_auth_request auth_req = deserialize_client_auth_request(buffer);
            server_auth_response auth_res;
            if (!strcmp(auth_req.payload.username, "user_test") && !strcmp(auth_req.payload.password, "pass_test"))
                auth_res = create_server_auth_response("success", "session_token_test", "Authentication successful");
            else if (!strcmp(auth_req.payload.username, "admin") && !strcmp(auth_req.payload.password, "admin"))
                auth_res = create_server_auth_response("success", "session_token_admin", "Authentication successful");
            else
                auth_res = create_server_auth_response("failure", "", "Authentication failed");

            char* serialized_response = serialize_server_auth_response(&auth_res);
            send(client_socket, serialized_response, strlen(serialized_response), 0);
            free(serialized_response);
        }
        else if (!strcmp(type, "warehouse_request_stock"))
        {
            warehouse_request_stock stock_req = deserialize_warehouse_request_stock(buffer);
            server_w_stock_warehouse stock_warehouse = create_server_w_stock_warehouse(stock_req.payload.items, 6);
            char* serialized_response = serialize_server_w_stock_warehouse(&stock_warehouse, 6);
            send(client_socket, serialized_response, strlen(serialized_response), 0);
            free(serialized_response);
        }
        else if (!strcmp(type, "hub_request_stock"))
        {
            hub_request_stock stock_req = deserialize_hub_request_stock(buffer);
            server_h_send_stock send_stock = create_server_h_send_stock("test_user", stock_req.payload.items, 6);
            char* serialized_response = serialize_server_h_send_stock(&send_stock, 6);
            send(client_socket, serialized_response, strlen(serialized_response), 0);
            free(serialized_response);
        }
        else if (!strcmp(type, "cli_message") && !strcmp(get_cli_type(buffer), "transactions_history"))
        {

            for (int i = 0; i < 5; i++)
            {
                inventory_item items[6] = {{"item1", 10}, {"item2", 20}, {"item3", 30},
                                           {"item4", 40}, {"item5", 50}, {"item6", 60}};
                server_transaction_history transaction =
                    create_server_transaction_history(i, i, i, "2025-04-21T16:00:00Z", "2025-04-21T16:00:00Z",
                                                      "2025-04-21T16:00:00Z", items, "warehouse", "hub");
                char* serialized_response = serialize_server_transaction_history(&transaction, 6);
                send(client_socket, serialized_response, strlen(serialized_response), 0);
                free(serialized_response);
                sleep(1);
            }
            end_of_message end_msg = create_end_of_message();
            char* serialized_end_msg = serialize_end_of_message(&end_msg);
            send(client_socket, serialized_end_msg, strlen(serialized_end_msg), 0);
            free(serialized_end_msg);
        }
        else if (!strcmp(type, "cli_message") && !strcmp(get_cli_type(buffer), "all_clients_live"))
        {
            for (int i = 0; i < 5; i++)
            {
                server_client_alive client_alive = create_server_client_alive("client", "warehouse", "connected");
                char* serialized_response = serialize_server_client_alive(&client_alive);
                send(client_socket, serialized_response, strlen(serialized_response), 0);
                free(serialized_response);
                sleep(1);
            }
            end_of_message end_msg = create_end_of_message();
            char* serialized_end_msg = serialize_end_of_message(&end_msg);
            send(client_socket, serialized_end_msg, strlen(serialized_end_msg), 0);
            free(serialized_end_msg);
        }
    }

    close(client_socket);
    return NULL;
}

int start_test_server(test_server_t* server, server_config_t config)
{
    if (!server)
        return -1;

    server->running = 1;
    server->config = config;

    if (config.family == SERVER_FAMILY_IPV4)
    {
        server->socket_fd = socket(AF_INET, (config.mode == SERVER_MODE_TCP) ? SOCK_STREAM : SOCK_DGRAM, 0);
        if (server->socket_fd < 0)
        {
            perror("socket");
            return -1;
        }

        int opt = 1;
        setsockopt(server->socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(config.port);

        if (bind(server->socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        {
            perror("bind");
            close(server->socket_fd);
            return -1;
        }

        if (config.mode == SERVER_MODE_TCP)
        {
            if (listen(server->socket_fd, 5) < 0)
            {
                perror("listen");
                close(server->socket_fd);
                return -1;
            }
        }
    }
    else
    {
        return -1; // IPv6 no soportado todavía
    }

    server_thread_arg_t* thread_arg = malloc(sizeof(server_thread_arg_t));
    thread_arg->server = server;

    pthread_create(&server->thread, NULL, server_thread_func, thread_arg);
    return 0;
}

void stop_test_server(test_server_t* server)
{
    if (!server || !server->running)
        return;

    server->running = 0;

    if (server->config.mode == SERVER_MODE_TCP && server->socket_fd >= 0)
    {
        int dummy_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (dummy_sock >= 0)
        {
            struct sockaddr_in addr = {0};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(server->config.port);
            connect(dummy_sock, (struct sockaddr*)&addr, sizeof(addr));
            close(dummy_sock);
        }
    }
    else if (server->config.mode == SERVER_MODE_UDP && server->socket_fd >= 0)
    {
        int dummy_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (dummy_sock >= 0)
        {
            struct sockaddr_in addr = {0};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(server->config.port);
            // mandamos algo para destrabar el recvfrom()
            sendto(dummy_sock, "server_quit", strlen("server_quit"), 0, (struct sockaddr*)&addr, sizeof(addr));
            close(dummy_sock);
        }
    }

    pthread_join(server->thread, NULL);

    if (server->socket_fd >= 0)
    {
        close(server->socket_fd);
        server->socket_fd = -1;
    }
}
