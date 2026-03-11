#include "worker.hpp"

#include "auth_module.hpp"
#include "connection_pool.hpp"
#include "database.hpp"
#include "inventory_manager.hpp"
#include "ipc.hpp"
#include "message_handler.hpp"

#include <iostream>
#include <thread>
#include <unistd.h>
#include <vector>

/**
 * Thread function: loops waiting for requests, processing them, and pushing responses.
 *
 * @param shm            Shared queue (shared-memory ring buffers).
 * @param handler        Message handler (thread-safe because auth_module and inventory_manager
 *                       access DB through connection_pool which is internally synchronized).
 * @param response_efd   eventfd to notify reactor of responses.
 * @param thread_id      Thread index (for logging).
 */
static void worker_thread_func(shared_queue& shm, message_handler& handler, int response_efd, int thread_id)
{
    std::cout << "[WORKER T" << thread_id << "] Thread started" << std::endl;

    request_slot_t request;

    while (shm.wait_request(request))
    {
        // Process the request
        std::vector<response_slot_t> responses = handler.process_request(request);

        // Push all responses back to the reactor
        for (const auto& resp : responses)
        {
            shm.push_response(resp, response_efd);
        }
    }

    std::cout << "[WORKER T" << thread_id << "] Thread exiting (shutdown signaled)" << std::endl;
}

void run_worker_process(int response_efd, const config::server_config& cfg)
{
    std::cout << "[WORKER] Worker process started (PID " << getpid() << ")" << std::endl;

    // Open shared memory segment (created by reactor before fork)
    shared_queue shm = shared_queue::open();

    // Create our own connection pool (worker process has its own DB connections)
    auto pool = std::make_shared<connection_pool>(build_connection_string(), cfg.pool_size);

    // Create processing modules
    auth_module auth(*pool);
    inventory_manager inv_mgr(*pool);
    message_handler handler(auth, inv_mgr, cfg.ack_timeout, cfg.max_retries, cfg.keepalive_timeout);

    // Spawn worker threads
    /// Fallback thread count when hardware_concurrency() returns 0.
    static constexpr std::uint32_t DEFAULT_WORKER_THREADS = 4;

    std::uint32_t num_threads = cfg.worker_threads;
    if (num_threads == 0)
    {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0)
        {
            num_threads = DEFAULT_WORKER_THREADS;
        }
    }

    std::cout << "[WORKER] Spawning " << num_threads << " worker threads" << std::endl;

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (std::uint32_t i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(worker_thread_func, std::ref(shm), std::ref(handler), response_efd, static_cast<int>(i));
    }

    // Wait for all threads to complete
    for (auto& t : threads)
    {
        t.join();
    }

    std::cout << "[WORKER] All threads finished, worker process exiting" << std::endl;
}
