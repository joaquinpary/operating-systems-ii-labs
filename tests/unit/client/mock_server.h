#ifndef MOCK_SERVER_H
#define MOCK_SERVER_H

#include <pthread.h>

// Structure to pass arguments to the mock server thread
typedef struct {
    int port;
    const char* response_msg;
} mock_server_args_t;

// Function to start the mock TCP server in a thread
void* mock_tcp_server(void* arg);

#endif
