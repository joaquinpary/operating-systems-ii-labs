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


// Later open .conf file
static void load_client_config(client_config *config) {
    strncpy(config->host, "127.0.0.1", sizeof(config->host) - 1);
    strncpy(config->port, "8080", sizeof(config->port) - 1);
    config->protocol = PROTO_UDP; // Default to UDP
    config->ip_version = AF_UNSPEC; // Allow IPv4 or IPv6
}

int client_init(client_context *ctx, client_config *config) {
    struct addrinfo hints, *res, *p;
    int status;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = config->ip_version;
    hints.ai_socktype = (config->protocol == PROTO_TCP) ? SOCK_STREAM : SOCK_DGRAM;

    if ((status = getaddrinfo(config->host, config->port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        if ((ctx->sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (config->protocol == PROTO_TCP) {
            if (connect(ctx->sockfd, p->ai_addr, p->ai_addrlen) == -1) {
                close(ctx->sockfd);
                perror("client: connect");
                continue;
            }
        } else {
            memcpy(&ctx->server_addr, p->ai_addr, p->ai_addrlen);
            ctx->addr_len = p->ai_addrlen;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to create socket\n");
        freeaddrinfo(res);
        return -1;
    }

    ctx->protocol = config->protocol;
    freeaddrinfo(res);
    return 0;
}

int client_send(client_context *ctx, const char *msg) {
    if (ctx->protocol == PROTO_TCP) {
        if (send(ctx->sockfd, msg, strlen(msg), MSG_NOSIGNAL) == -1) {
            perror("send");
            return -1;
        }
    } else {
        if (sendto(ctx->sockfd, msg, strlen(msg), 0, 
                  (struct sockaddr *)&ctx->server_addr, ctx->addr_len) == -1) {
            perror("sendto");
            return -1;
        }
    }
    return 0;
}

int client_receive(client_context *ctx, char *buffer, size_t buffer_size) {
    ssize_t num_bytes;
    memset(buffer, 0, buffer_size);

    if (ctx->protocol == PROTO_TCP) {
        if ((num_bytes = recv(ctx->sockfd, buffer, buffer_size - 1, 0)) == -1) {
            perror("recv");
            return -1;
        }
        if (num_bytes == 0) {
            return -2; // Connection closed by server
        }
    } else {
        struct sockaddr_storage from_addr;
        socklen_t from_len = sizeof(from_addr);
        if ((num_bytes = recvfrom(ctx->sockfd, buffer, buffer_size - 1, 0, 
                                (struct sockaddr *)&from_addr, &from_len)) == -1) {
            perror("recvfrom");
            return -1;
        }
    }

    if (num_bytes >= 0 && (size_t)num_bytes < buffer_size) {
        buffer[num_bytes] = '\0';
    } else if (buffer_size > 0) {
        buffer[buffer_size - 1] = '\0';
    }
    
    return (int)num_bytes;
}

void client_close(client_context *ctx) {
    if (ctx->sockfd >= 0) {
        close(ctx->sockfd);
        ctx->sockfd = -1;
    }
}

int init_connection(client_context *ctx) {
    client_config config;

    load_client_config(&config);

    if (client_init(ctx, &config) != 0) {
        return -1;
    }

    // Remove this later
    printf("Connected to %s:%s via %s\n", config.host, config.port, 
           (config.protocol == PROTO_TCP) ? "TCP" : "UDP");

    return 0;
}
