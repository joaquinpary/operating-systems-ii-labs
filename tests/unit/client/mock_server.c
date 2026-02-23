#include "mock_server.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

void* mock_tcp_server(void* arg)
{
    mock_server_args_t* args = (mock_server_args_t*)arg;
    int port = args ? args->port : 8081;
    const char* response = args ? args->response_msg : "Hello from server";
    sem_t* ready_sem = args ? args->ready_sem : NULL;
    mock_behavior_t behavior = args ? args->behavior : MOCK_BEHAVIOR_NORMAL;

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char recv_buffer[BUFFER_SIZE] = {0};
    char send_buffer[BUFFER_SIZE] = {0};

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("mock_server: socket failed");
        if (ready_sem) sem_post(ready_sem);
        return NULL;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("mock_server: setsockopt");
        close(server_fd);
        if (ready_sem) sem_post(ready_sem);
        return NULL;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0)
    {
        perror("mock_server: bind failed");
        close(server_fd);
        if (ready_sem) sem_post(ready_sem);
        return NULL;
    }

    if (listen(server_fd, 3) < 0)
    {
        perror("mock_server: listen");
        close(server_fd);
        if (ready_sem) sem_post(ready_sem);
        return NULL;
    }

    // Signal that server is ready to accept connections
    if (ready_sem) sem_post(ready_sem);

    if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0)
    {
        perror("mock_server: accept");
        close(server_fd);
        return NULL;
    }

    // Handle different behaviors
    if (behavior == MOCK_BEHAVIOR_CLOSE_IMMEDIATE)
    {
        close(new_socket);
        close(server_fd);
        return NULL;
    }

    read(new_socket, recv_buffer, BUFFER_SIZE);

    memset(send_buffer, 0, BUFFER_SIZE);
    strncpy(send_buffer, response, BUFFER_SIZE - 1);
    send(new_socket, send_buffer, BUFFER_SIZE, 0);

    close(new_socket);
    close(server_fd);
    return NULL;
}

void* mock_udp_server(void* arg)
{
    mock_server_args_t* args = (mock_server_args_t*)arg;
    int port = args ? args->port : 8082;
    const char* response = args ? args->response_msg : "Hello from UDP server";
    sem_t* ready_sem = args ? args->ready_sem : NULL;

    int server_fd;
    struct sockaddr_in address, client_addr;
    int opt = 1;
    socklen_t client_len = sizeof(client_addr);
    char recv_buffer[BUFFER_SIZE] = {0};

    if ((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("mock_udp_server: socket failed");
        if (ready_sem) sem_post(ready_sem);
        return NULL;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("mock_udp_server: setsockopt");
        close(server_fd);
        if (ready_sem) sem_post(ready_sem);
        return NULL;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0)
    {
        perror("mock_udp_server: bind failed");
        close(server_fd);
        if (ready_sem) sem_post(ready_sem);
        return NULL;
    }

    // Signal that server is ready
    if (ready_sem) sem_post(ready_sem);

    // Wait for incoming message
    ssize_t recv_len = recvfrom(server_fd, recv_buffer, BUFFER_SIZE - 1, 0,
                                 (struct sockaddr*)&client_addr, &client_len);

    if (recv_len > 0)
    {
        recv_buffer[recv_len] = '\0';
        sendto(server_fd, response, strlen(response), 0,
               (struct sockaddr*)&client_addr, client_len);
    }

    close(server_fd);
    return NULL;
}
