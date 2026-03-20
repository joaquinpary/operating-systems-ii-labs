#define _POSIX_C_SOURCE 200809L

#include "connection.h"
#include "json_manager.h"
#include "logger.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static const char* protocol_to_string(protocol_type protocol)
{
    return protocol == PROTO_TCP ? "TCP" : "UDP";
}

/**
 * @brief Initialize the client socket and server addressing data.
 */
int client_init(client_context* ctx, client_config* config)
{
    struct addrinfo hints, *res, *p;
    int status;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = config->ip_version;
    hints.ai_socktype = (config->protocol == PROTO_TCP) ? SOCK_STREAM : SOCK_DGRAM;

    if ((status = getaddrinfo(config->host, config->port, &hints, &res)) != 0)
    {
        LOG_ERROR_MSG("getaddrinfo failed for %s:%s: %s", config->host, config->port, gai_strerror(status));
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next)
    {
        if ((ctx->sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            LOG_ERROR_MSG("socket creation failed: %s", strerror(errno));
            continue;
        }

        if (config->protocol == PROTO_TCP)
        {
            if (connect(ctx->sockfd, p->ai_addr, p->ai_addrlen) == -1)
            {
                close(ctx->sockfd);
                LOG_ERROR_MSG("connect failed for %s:%s: %s", config->host, config->port, strerror(errno));
                continue;
            }
        }
        else
        {
            memcpy(&ctx->server_addr, p->ai_addr, p->ai_addrlen);
            ctx->addr_len = p->ai_addrlen;
        }
        break;
    }

    if (p == NULL)
    {
        LOG_ERROR_MSG("Unable to establish %s connection to %s:%s", protocol_to_string(config->protocol), config->host,
                      config->port);
        freeaddrinfo(res);
        return -1;
    }

    ctx->protocol = config->protocol;
    freeaddrinfo(res);
    LOG_INFO_MSG("Connected to %s:%s using %s", config->host, config->port, protocol_to_string(config->protocol));
    return 0;
}

/**
 * @brief Send a serialized message to the configured server.
 */
int client_send(client_context* ctx, const char* msg)
{
    if (ctx->protocol == PROTO_TCP)
    {
        char* tcp_msg = malloc(BUFFER_SIZE);
        if (!tcp_msg)
        {
            LOG_ERROR_MSG("Failed to allocate TCP send buffer: %s", strerror(errno));
            return -1;
        }
        memset(tcp_msg, 0, BUFFER_SIZE);
        snprintf(tcp_msg, BUFFER_SIZE, "%s", msg);
        if (send(ctx->sockfd, tcp_msg, BUFFER_SIZE, MSG_NOSIGNAL) == -1)
        {
            LOG_ERROR_MSG("send failed: %s", strerror(errno));
            free(tcp_msg);
            return -1;
        }

        free(tcp_msg);
    }
    else
    {
        if (sendto(ctx->sockfd, msg, strlen(msg), 0, (struct sockaddr*)&ctx->server_addr, ctx->addr_len) == -1)
        {
            LOG_ERROR_MSG("sendto failed: %s", strerror(errno));
            return -1;
        }
    }
    return 0;
}

/**
 * @brief Receive a serialized message from the server.
 */
int client_receive(client_context* ctx, char* buffer, size_t buffer_size)
{
    ssize_t num_bytes;
    memset(buffer, 0, buffer_size);

    if (ctx->protocol == PROTO_TCP)
    {
        size_t total_received = 0;
        while (total_received < BUFFER_SIZE)
        {
            ssize_t n = recv(ctx->sockfd, buffer + total_received, BUFFER_SIZE - total_received, 0);
            if (n == -1)
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    LOG_ERROR_MSG("recv failed: %s", strerror(errno));
                }
                return -1;
            }
            if (n == 0)
            {
                return -2;
            }
            total_received += n;
        }
        num_bytes = total_received;
        buffer[BUFFER_SIZE - 1] = '\0';
    }
    else
    {
        struct sockaddr_storage from_addr;
        socklen_t from_len = sizeof(from_addr);
        if ((num_bytes = recvfrom(ctx->sockfd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr*)&from_addr, &from_len)) ==
            -1)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
                LOG_ERROR_MSG("recvfrom failed: %s", strerror(errno));
            }
            return -1;
        }
        if (num_bytes >= 0 && num_bytes < BUFFER_SIZE)
        {
            buffer[num_bytes] = '\0';
        }
    }

    return (int)num_bytes;
}

/**
 * @brief Close the active client socket if present.
 */
void client_close(client_context* ctx)
{
    if (ctx->sockfd >= 0)
    {
        close(ctx->sockfd);
        ctx->sockfd = -1;
    }
}
