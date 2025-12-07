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

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char recv_buffer[BUFFER_SIZE] = {0};
    char send_buffer[BUFFER_SIZE] = {0};

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("mock_server: socket failed");
        return NULL;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("mock_server: setsockopt");
        return NULL;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0)
    {
        perror("mock_server: bind failed");
        return NULL;
    }

    if (listen(server_fd, 3) < 0)
    {
        perror("mock_server: listen");
        return NULL;
    }

    if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0)
    {
        perror("mock_server: accept");
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
