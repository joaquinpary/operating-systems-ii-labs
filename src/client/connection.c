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

#define BUFFER_SIZE 256
#define MIN_SIZE 64
#define PATH_CONFIG "config/config_launch.json"
#define SECTION "client"
#define UDP "udp"
#define TCP "tcp"
#define IPV4 "ipv4"
#define IPV6 "ipv6"

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
        return -1;
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0)
    {
        perror("socket");
        freeaddrinfo(res);
        return -1;
    }

    memcpy(dest_addr, res->ai_addr, res->ai_addrlen);
    *addr_len = res->ai_addrlen;

    freeaddrinfo(res);
    return sockfd;
}

int send_packet_udp(int sockfd, char* buffer, struct sockaddr_storage* dest_addr, socklen_t addr_len)
{
    int num_fd;

    num_fd = sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr*)dest_addr, addr_len);
    if (num_fd < 0)
    {
        perror("Error sendto");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    memset(buffer, 0, BUFFER_SIZE);
    num_fd = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0, NULL, NULL); // Dont care about the source address
    if (num_fd < 0)
    {
        perror("Error recvfrom");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Response: %s\n", buffer);

    return num_fd;
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

int send_packet_tcp(int sockfd, char* buffer)
{
    int num_fd, finish = 0;

    num_fd = write(sockfd, buffer, strlen(buffer));
    if (num_fd < 0)
    {
        perror("Error write");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    buffer[strlen(buffer) - 1] = '\0';
    if (!strcmp("quit", buffer))
    {
        finish = 1;
    }

    memset(buffer, '\0', BUFFER_SIZE);
    num_fd = read(sockfd, buffer, BUFFER_SIZE);
    if (num_fd < 0)
    {
        perror("Error read");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("Response: %s\n", buffer);

    if (finish)
    {
        printf("Finish execution\n");
        close(sockfd);
        exit(0);
    }
    return num_fd;
}

int init_connection(void)
{
    char buffer[BUFFER_SIZE];
    int num_fd;
    int sockfd;
    init_params_client params = load_config_client(PATH_CONFIG, SECTION);
    char* ip_version = params.ip_version;
    char* protocol = params.protocol;
    char* ip_address = params.host;
    char* port = params.port;
    if (!strcmp(protocol, UDP))
    {
        struct sockaddr_storage dest_addr;
        socklen_t addr_len;
        sockfd = setup_socket_udp(ip_version, ip_address, port, &dest_addr, &addr_len);

        printf("Enter the message: ");
        memset(buffer, '\0', BUFFER_SIZE);
        fgets(buffer, BUFFER_SIZE - 1, stdin);

        num_fd = send_packet_udp(sockfd, buffer, &dest_addr, addr_len);
    }
    else if (!strcmp(protocol, TCP))
    {
        sockfd = setup_socket_tpc(ip_version, ip_address, port);
        while (1)
        {
            printf("Enter the message: ");
            memset(buffer, '\0', BUFFER_SIZE);
            fgets(buffer, BUFFER_SIZE - 1, stdin);

            num_fd = send_packet_tcp(sockfd, buffer);
        }
    }
    return 0;
}
