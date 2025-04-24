#define _POSIX_C_SOURCE 200112L

#include "connection.h"
#include "json_manager.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int setup_socket_udp(const char* ip_version, const char* ip_address, const char* port,
                     struct sockaddr_storage* dest_addr, socklen_t* addr_len)
{
    int sockfd;
    struct addrinfo hints, *res;
    int status;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = (strcmp(ip_version, IPV6) == 0) ? AF_INET6 : AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if ((status = getaddrinfo(ip_address, port, &hints, &res)) != 0)
    {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return 1;
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0)
    {
        perror("socket");
        freeaddrinfo(res);
        return 1;
    }

    memcpy(dest_addr, res->ai_addr, res->ai_addrlen);
    *addr_len = res->ai_addrlen;

    freeaddrinfo(res);
    return sockfd;
}

int sender_udp(int sockfd, char* buffer, struct sockaddr_storage* dest_addr, socklen_t addr_len)
{
    int bytes_send_udp;
    bytes_send_udp = sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr*)dest_addr, addr_len);
    if (bytes_send_udp < 0)
    {
        perror("Error sendto");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    return bytes_send_udp;
}

int receiver_udp(int sockfd, char* buffer, struct sockaddr_storage* dest_addr, socklen_t addr_len)
{
    int bytes_recv_udp;
    bytes_recv_udp = recvfrom(sockfd, buffer, SOCKET_SIZE - 1, 0, NULL, NULL); // Dont care about the source address
    //bytes_recv_udp = recvfrom(sockfd, buffer, SOCKET_SIZE - 1, 0, (struct sockaddr*)dest_addr, &addr_len);

    if (bytes_recv_udp < 0)
    {
        perror("Error recvfrom");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    //printf("Response: %s\n", buffer); // Later, comment this line
    return bytes_recv_udp;
}

int setup_socket_tpc(const char* ip_version, const char* ip_address, const char* port_number)
{
    int sockfd;
    struct addrinfo hints, *res;
    int status;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = (strcmp(ip_version, IPV6) == 0) ? AF_INET6 : AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(ip_address, port_number, &hints, &res)) != 0)
    {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0)
    {
        perror("socket");
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0)
    {
        perror("connect");
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(res);
    return sockfd;
}

int sender_tpc(int sockfd, char* buffer)
{
    int bytes_send_tcp, finish = 0;
    bytes_send_tcp = write(sockfd, buffer, strlen(buffer));
    if (bytes_send_tcp < 0)
    {
        perror("Error write");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    return bytes_send_tcp;
}

int receiver_tcp(int sockfd, char* buffer)
{
    int bytes_recv_tcp;
    bytes_recv_tcp = read(sockfd, buffer, SOCKET_SIZE);
    if (bytes_recv_tcp < 0)
    {
        perror("Error read");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    buffer[bytes_recv_tcp] = '\0';
    //printf("Response: %s\n", buffer);
    return bytes_recv_tcp;
}

connection_context init_connection_udp(init_params_client params)
{
    int sockfd;
    struct sockaddr_storage dest_addr;
    socklen_t addr_len;
    sockfd = setup_socket_udp(params.connection_params.ip_version, params.connection_params.host,
                              params.connection_params.port, &dest_addr, &addr_len);
    if (sockfd < 0)
    {
        fprintf(stderr, "Error setting up UDP socket\n");
        exit(EXIT_FAILURE);
    }
    connection_context context = {.sockfd = sockfd, .dest_addr = dest_addr, .addr_len = addr_len};
    return context;
}

int init_connection_tcp(init_params_client params)
{
    int sockfd;
    sockfd = setup_socket_tpc(params.connection_params.ip_version, params.connection_params.host,
                              params.connection_params.port);
    if (sockfd < 0)
    {
        fprintf(stderr, "Error setting up TCP socket\n");
        exit(EXIT_FAILURE);
    }
    return sockfd;
}
