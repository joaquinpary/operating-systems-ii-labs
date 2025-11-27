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

typedef enum {
    PROTO_TCP,
    PROTO_UDP
} protocol_type;

typedef struct {
    int sockfd;
    protocol_type protocol;
    struct sockaddr_storage server_addr;
    socklen_t addr_len;
} client_context;

typedef struct {
    char host[256];
    char port[16];
    protocol_type protocol;
    int ip_version; // AF_INET, AF_INET6, or AF_UNSPEC
} client_config;

/* @brief
 * Establishes a connection to the server based on the provided configuration.
 * @param ctx Pointer to the client context to be initialized.
 * @param config Pointer to the client configuration.
 * @return 0 on success, -1 on failure.
*/
int client_init(client_context *ctx, client_config *config);


/* @brief
 * Sends a message to the server.
 * @param ctx Pointer to the client context.
 * @param msg The message to send.
 * @return 0 on success, -1 on failure.
 */
int client_send(client_context *ctx, const char *msg);

/* @brief
 * Receives a message from the server.
 * @param ctx Pointer to the client context.
 * @param buffer Buffer to store the received message.
 * @param buffer_size Size of the buffer.
 * @return Number of bytes received, -1 on error, -2 if connection closed by server.
 */
int client_receive(client_context *ctx, char *buffer, size_t buffer_size);

/* @brief
 * Closes the client connection.
 * @param ctx Pointer to the client context.
 */
void client_close(client_context *ctx);

/* @brief
 * Initializes the client connection with default configuration.
 * @param ctx Pointer to the client context.
 * @return 0 on success, -1 on failure.
 */
int init_connection(client_context *ctx);

#endif
