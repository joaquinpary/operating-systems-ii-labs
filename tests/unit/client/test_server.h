#ifndef TEST_SERVER_H
#define TEST_SERVER_H

#include <pthread.h>

#pragma once

typedef enum
{
    SERVER_MODE_TCP,
    SERVER_MODE_UDP
} server_mode_t;

typedef enum
{
    SERVER_FAMILY_IPV4,
    SERVER_FAMILY_IPV6
} server_family_t;

typedef struct
{
    server_mode_t mode;
    server_family_t family;
    int port;
} server_config_t;

typedef struct
{
    pthread_t thread;
    int running;
    int socket_fd;
    server_config_t config;
} test_server_t;

int start_test_server(test_server_t* server, server_config_t config);
void stop_test_server(test_server_t* server);

#endif
