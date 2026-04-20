#ifndef LOGIC_H
#define LOGIC_H

#include "connection.h"

/**
 * @brief Initialize and start the client logic with networking and business logic.
 *
 * This function:
 * - Sets up IPC (shared memory, semaphores)
 * - Forks a child process for business logic
 * - Creates threads for receiving, sending, and ACK checking
 * - Blocks until the child process exits
 *
 * @param ctx Client context with socket connection
 * @param client_role Client type: "HUB" or "WAREHOUSE"
 * @param client_id Client identifier (username)
 * @return 0 on success, -1 on error
 */
int logic_init(client_context* ctx, const char* client_role, const char* client_id);

#endif
