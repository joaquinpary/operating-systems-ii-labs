#ifndef WORKER_HPP
#define WORKER_HPP

#include "config.hpp"
#include <cstdint>

/**
 * Entry point for the worker process (called after fork()).
 *
 * Opens the shared memory segment created by the reactor,
 * creates its own connection_pool, auth_module, inventory_manager,
 * message_handler, and spawns `num_threads` worker threads.
 *
 * Each thread loops on shared_queue::wait_request() → process → push_response().
 * Returns when shutdown is signaled.
 *
 * @param response_efd  File descriptor of the eventfd for worker→reactor notification.
 * @param cfg           Server configuration (for DB pool, ack_timeout, max_retries, etc.).
 */
void run_worker_process(int response_efd, const config::server_config& cfg);

#endif // WORKER_HPP
