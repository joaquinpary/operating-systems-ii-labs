#ifndef CONNECTION_H
#define CONNECTION_H

#include "cJSON.h"
#include "json_manager.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define SOCKET_SIZE 1024
#define BUFFER_SIZE 256
#define MIN_SIZE 64
#define PATH_CONFIG "config/config_launch.json"
#define SECTION "client"
#define UDP "udp"
#define TCP "tcp"
#define IPV4 "ipv4"
#define IPV6 "ipv6"

typedef struct
{
    int sockfd;
    struct sockaddr_storage dest_addr;
    socklen_t addr_len;
} connection_context;

int setup_socket_udp(const char* ip_version, const char* ip_address, const char* port,
                     struct sockaddr_storage* dest_addr, socklen_t* addr_len);
int sender_udp(int sockfd, char* buffer, struct sockaddr_storage* dest_addr, socklen_t addr_len);
int receiver_udp(int sockfd, char* buffer, struct sockaddr_storage* dest_addr, socklen_t addr_len);
int setup_socket_tpc(const char* ip_version, const char* ip_address, const char* port);
int sender_tpc(int sockfd, char* buffer);
int receiver_tcp(int sockfd, char* buffer);
connection_context init_connection_udp(init_params_client params);
int init_connection_tcp(init_params_client params);

#endif
