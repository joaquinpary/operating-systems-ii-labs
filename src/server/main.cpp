#include "config.hpp"
#include "connection_pool.hpp"
#include "database.hpp"
#include "event_loop.hpp"
#include "ipc.hpp"
#include "server.hpp"
#include "session_manager.hpp"
#include "timer_manager.hpp"
#include "worker.hpp"

#include <common/logger.h>

#include <csignal>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unistd.h>

/// Default path to the server configuration file.
static constexpr const char* SERVER_CONFIG_DEFAULT = "config/server_config.json";

int main()
{
    try
    {
        // ── Load configuration ──────────────────────────────────────────
        config::server_config cfg;
        config::load_config_from_file(SERVER_CONFIG_DEFAULT, cfg);

        // ── Initialize database schema (once, before fork) ──────────────
        {
            auto pool = std::make_shared<connection_pool>(build_connection_string(), cfg.pool_size);
            auto guard = pool->acquire();
            if (initialize_database(guard.get(), cfg.credentials_path) != 0)
            {
                throw std::runtime_error("Failed to initialize database. Server cannot start.");
            }
            // Ensure all clients start as inactive on fresh boot
            reset_all_clients_inactive(guard.get());
        }

        // ── Create shared memory IPC ────────────────────────────────────
        shared_queue shm = shared_queue::create();

        // ── Create eventfd for worker→reactor notification ──────────────
        int response_efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (response_efd == -1)
        {
            throw std::runtime_error(std::string("eventfd: ") + strerror(errno));
        }

        // ── Block SIGINT/SIGTERM before fork (inherited by child) ───────
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTERM);
        if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1)
        {
            throw std::runtime_error(std::string("sigprocmask: ") + strerror(errno));
        }

        // ── Fork: child = worker process, parent = reactor ──────────────
        pid_t worker_pid = fork();

        if (worker_pid == -1)
        {
            throw std::runtime_error(std::string("fork: ") + strerror(errno));
        }

        if (worker_pid == 0)
        {
            // ── CHILD: Worker process ───────────────────────────────────
            // Unblock signals in worker (so it can be killed normally)
            sigprocmask(SIG_UNBLOCK, &mask, nullptr);

            run_worker_process(response_efd, cfg);
            _exit(0);
        }

        // ── PARENT: Reactor process ─────────────────────────────────────

        // Initialize logger for the reactor process
        {
            const char* log_dir = std::getenv("LOG_DIR");
            if (!log_dir) log_dir = "logs/server";
            logger_config_t log_cfg = {.max_file_size = 10 * 1024 * 1024, .max_backup_files = 5, .min_level = LOG_DEBUG};
            snprintf(log_cfg.log_file_path, sizeof(log_cfg.log_file_path), "%s/server_reactor.log", log_dir);
            log_init(&log_cfg);
        }

        LOG_INFO_MSG("[REACTOR] pid=%d worker_pid=%d", getpid(), worker_pid);
        // Create signalfd for clean shutdown
        int sig_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
        if (sig_fd == -1)
        {
            throw std::runtime_error(std::string("signalfd: ") + strerror(errno));
        }

        // Create event loop
        event_loop loop;

        // Create reactor-side modules
        auto session_mgr = std::make_unique<session_manager>();
        auto timer_mgr = std::make_unique<timer_manager>(loop);

        // Create server
        server srv(loop, shm, response_efd, cfg, std::move(session_mgr), std::move(timer_mgr));

        // Register signalfd in epoll for graceful shutdown
        loop.add_fd(sig_fd, EPOLLIN, [&](std::uint32_t) {
            struct signalfd_siginfo info;
            if (read(sig_fd, &info, sizeof(info)) == sizeof(info))
            {
                LOG_INFO_MSG("[REACTOR] signal=%u shutting down", info.ssi_signo);
            }
            srv.stop();
            shm.signal_shutdown();
            loop.stop();
        });

        // Start accepting connections
        srv.start();

        LOG_INFO_MSG("[REACTOR] Server running, waiting for connections");
        // Run the event loop (blocks until loop.stop())
        loop.run();

        // ── Cleanup ─────────────────────────────────────────────────────
        LOG_INFO_MSG("[REACTOR] Event loop stopped, waiting for worker");
        // Wait for worker process to exit
        int status;
        waitpid(worker_pid, &status, 0);
        if (WIFEXITED(status))
        {
            LOG_INFO_MSG("[REACTOR] Worker exited status=%d", WEXITSTATUS(status));
        }

        // Close signalfd and eventfd
        close(sig_fd);
        close(response_efd);

        // Unlink shared memory
        shared_queue::unlink();

        LOG_INFO_MSG("[REACTOR] Shutdown complete");
        log_close();
    }
    catch (const std::exception& ex)
    {
        LOG_ERROR_MSG("[REACTOR] Fatal: %s", ex.what());
        std::cerr << "Fatal: " << ex.what() << '\n';
        log_close();
        return 1;
    }

    return 0;
}
