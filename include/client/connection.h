#ifndef CONNECTION_H
#define CONNECTION_H

#include "json_manager.h"
#include <sys/socket.h>
#include <sys/types.h>

#define SOCKET_SIZE 1024
#define BUFFER_SIZE 256
#define MIN_SIZE 32
#define PATH_CONFIG "config/clients_credentials.json"
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

/*
 * @brief Setup a UDP socket.
 * @param ip_version IP version (IPv4 or IPv6).
 * @param ip_address IP address.
 * @param port Port number.
 * @param dest_addr Destination address structure.
 * @param addr_len Length of the destination address structure.
 * @return Socket file descriptor.
 */
int setup_socket_udp(const char* ip_version, const char* ip_address, const char* port,
                     struct sockaddr_storage* dest_addr, socklen_t* addr_len);

/*
 * @brief Function to send data over UDP.
 * @param sockfd Socket file descriptor.
 * @param buffer Buffer containing the data to be sent.
 * @param dest_addr Destination address structure.
 * @param addr_len Length of the destination address structure.
 * @return Number of bytes sent.
 */
int sender_udp(int sockfd, char* buffer, struct sockaddr_storage* dest_addr, socklen_t addr_len);

/*
 * @brief Function to receive data over UDP.
 * @param sockfd Socket file descriptor.
 * @param buffer Buffer to store the received data.
 * @param dest_addr Destination address structure.
 * @param addr_len Length of the destination address structure.
 * @return Number of bytes received.
 */
int receiver_udp(int sockfd, char* buffer, struct sockaddr_storage* dest_addr, socklen_t addr_len);

/*
 * @brief Function to set up a TCP socket.
 * @param ip_version IP version (IPv4 or IPv6).
 * @param ip_address IP address.
 * @param port Port number.
 * @return Socket file descriptor.
 */
int setup_socket_tpc(const char* ip_version, const char* ip_address, const char* port);

/*
 * @brief Function to send data over TCP.
 * @param sockfd Socket file descriptor.
 * @param buffer Buffer containing the data to be sent.
 * @return Number of bytes sent.
 */
int sender_tpc(int sockfd, char* buffer);

/*
 * @brief Function to receive data over TCP.
 * @param sockfd Socket file descriptor.
 * @param buffer Buffer to store the received data.
 * @return Number of bytes received.
 */
int receiver_tcp(int sockfd, char* buffer);

/*
 * @brief Function to initialize a UDP connection.
 * @param params Parameters for client initialization.
 * @return Connection context containing socket file descriptor and address information.
 */
connection_context init_connection_udp(init_params_client params);

/*
 * @brief Function to initialize a TCP connection.
 * @param params Parameters for client initialization.
 * @return Socket file descriptor.
 */
int init_connection_tcp(init_params_client params);

#endif
