#ifndef MOCK_SERVER_H
#define MOCK_SERVER_H

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

// Behavior flags for simulating different scenarios
typedef enum
{
    MOCK_BEHAVIOR_NORMAL,         // Normal response
    MOCK_BEHAVIOR_CLOSE_IMMEDIATE // Close connection immediately (simulate disconnect)
} mock_behavior_t;

// Structure to pass arguments to the mock server thread
typedef struct
{
    int port;
    const char* response_msg;
    sem_t* ready_sem;         // Semaphore to signal when server is ready (optional)
    mock_behavior_t behavior; // Behavior mode for error simulation
} mock_server_args_t;

// Function to start the mock TCP server in a thread
void* mock_tcp_server(void* arg);

// Function to start the mock UDP server in a thread
void* mock_udp_server(void* arg);

#endif
