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

#define BUFFER_SIZE 256
#define MIN_SIZE 64

int setup_socket_udp(const char* ip_version, const char* ip_address, const char* port,
                     struct sockaddr_storage* dest_addr, socklen_t* addr_len);
int send_packet_udp(int sockfd, char* buffer, struct sockaddr_storage* dest_addr, socklen_t addr_len);
int setup_socket_tpc(const char* ip_version, const char* ip_address, const char* port);
int send_packet_tcp(int sockfd, char* buffer);
int init_connection(void);

#endif
