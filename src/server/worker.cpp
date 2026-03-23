#include "worker.hpp"

#include "auth_module.hpp"
#include "connection_pool.hpp"
#include "database.hpp"
#include "inventory_manager.hpp"
#include "ipc.hpp"
#include "message_handler.hpp"

#include <common/json_manager.h>
#include <common/logger.h>

#include <cstring>
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
    LOG_INFO_MSG("[T%d] Thread started", thread_id);
    request_slot_t request;

    while (shm.wait_request(request))
    {
        // Deserialize once for logging (type, timestamp, source)
        message_t log_msg;
        bool deserialized = (deserialize_message_from_json(request.raw_json, &log_msg) == 0);

        if (deserialized)
        {
            LOG_INFO_MSG("[T%d] IN type=%s ts=%s from=%s sess=%s", thread_id, log_msg.msg_type, log_msg.timestamp,
                         log_msg.source_id, request.session_id);
        }
        else if (request.is_disconnect)
        {
            LOG_INFO_MSG("[T%d] IN disconnect user=%s sess=%s", thread_id, request.username, request.session_id);
        }

        // 1. Send ACK immediately (lightweight — no DB access)
        auto ack = handler.generate_ack(request);
        if (ack)
        {
            shm.push_response(*ack, response_efd);
            if (deserialized)
            {
                LOG_DEBUG_MSG("[T%d] ACK pushed ts=%s -> sess=%s", thread_id, log_msg.timestamp, request.session_id);
            }
        }

        // 2. Process the request (heavy business logic — DB queries, inventory, etc.)
        std::vector<response_slot_t> responses = handler.process_request(request);

        // 3. Push all remaining responses back to the reactor
        for (const auto& resp : responses)
        {
            shm.push_response(resp, response_efd);
            LOG_DEBUG_MSG("[T%d] OUT cmd=%u -> sess=%s", thread_id, static_cast<unsigned>(resp.command),
                          resp.session_id);
        }

        LOG_INFO_MSG("[T%d] DONE type=%s sess=%s", thread_id, deserialized ? log_msg.msg_type : "DISCONNECT",
                     request.session_id);
    }

    LOG_INFO_MSG("[T%d] Thread exiting", thread_id);
}

void run_worker_process(int response_efd, const config::server_config& cfg)
{
    // Initialize logger for the worker process (separate file from reactor)
    {
        const char* log_dir = std::getenv("LOG_DIR");
        if (!log_dir)
            log_dir = "logs/server";
        logger_config_t log_cfg = {.max_file_size = 50 * 1024 * 1024, .max_backup_files = 1000, .min_level = LOG_DEBUG};
        snprintf(log_cfg.log_file_path, sizeof(log_cfg.log_file_path), "%s/server_worker.log", log_dir);
        log_init(&log_cfg);
    }

    LOG_INFO_MSG("[WORKER] pid=%d started", getpid());
    // Open shared memory segment (created by reactor before fork)
    shared_queue shm = shared_queue::open();

    // Create our own connection pool (worker process has its own DB connections)
    auto pool = std::make_shared<connection_pool>(build_connection_string(), cfg.pool_size);

    // Reset all clients to inactive on startup
    {
        auto guard = pool->acquire();
        reset_all_clients_inactive(guard.get());
    }

    // Create processing modules (direct DB access via pool)
    auth_module auth(*pool);
    inventory_manager inv_mgr(*pool);
    message_handler handler(auth, inv_mgr, cfg.ack_timeout, cfg.max_retries, cfg.keepalive_timeout,
                            build_connection_string());

    // Spawn worker threads
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

    LOG_INFO_MSG("[WORKER] Spawning %u threads", num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (std::uint32_t i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(worker_thread_func, std::ref(shm), std::ref(handler), response_efd, static_cast<int>(i));
    }

    for (auto& t : threads)
    {
        t.join();
    }

    LOG_INFO_MSG("[WORKER] All threads finished, exiting");
    log_close();
}
