#include "api_rest.hpp"
#include "config.hpp"
#include "connection_pool.hpp"
#include "database.hpp"
#include "event_loop.hpp"
#include "ipc.hpp"
#include "mqtt_client.hpp"
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

static constexpr size_t SERVER_LOG_MAX_FILE_SIZE = 50 * 1024 * 1024;
static constexpr int SERVER_LOG_MAX_BACKUPS = 1000;

/// Default path to the server configuration file.
static constexpr const char* SERVER_CONFIG_DEFAULT = "config/server_config.json";

int main()
{
    try
    {
        config::server_config cfg;
        config::load_config_from_file(SERVER_CONFIG_DEFAULT, cfg);

        {
            auto pool = std::make_shared<connection_pool>(build_connection_string(), cfg.pool_size);
            auto guard = pool->acquire();
            if (initialize_database(guard.get(), cfg.credentials_path) != 0)
            {
                throw std::runtime_error("Failed to initialize database. Server cannot start.");
            }
            reset_all_clients_inactive(guard.get());
        }

        shared_queue shm = shared_queue::create();

        int response_efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (response_efd == -1)
        {
            throw std::runtime_error(std::string("eventfd: ") + strerror(errno));
        }

        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTERM);
        if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1)
        {
            throw std::runtime_error(std::string("sigprocmask: ") + strerror(errno));
        }

        pid_t worker_pid = fork();

        if (worker_pid == -1)
        {
            throw std::runtime_error(std::string("fork: ") + strerror(errno));
        }

        if (worker_pid == 0)
        {
            sigprocmask(SIG_UNBLOCK, &mask, nullptr);

            run_worker_process(response_efd, cfg);
            _exit(0);
        }

        pid_t api_rest_pid = fork();
        if (api_rest_pid == -1)
        {
            throw std::runtime_error(std::string("fork api_rest: ") + strerror(errno));
        }

        if (api_rest_pid == 0)
        {
            sigprocmask(SIG_UNBLOCK, &mask, nullptr);

            run_api_rest_process(cfg);
            _exit(0);
        }

        {
            const char* log_dir = std::getenv("LOG_DIR");
            if (!log_dir)
                log_dir = "logs/server";
            logger_config_t log_cfg = {.max_file_size = SERVER_LOG_MAX_FILE_SIZE,
                                       .max_backup_files = SERVER_LOG_MAX_BACKUPS,
                                       .min_level = LOG_DEBUG};
            snprintf(log_cfg.log_file_path, sizeof(log_cfg.log_file_path), "%s/server_reactor.log", log_dir);
            log_init(&log_cfg);
        }

        LOG_INFO_MSG("[REACTOR] pid=%d worker_pid=%d api_rest_pid=%d", getpid(), worker_pid, api_rest_pid);

        int sig_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
        if (sig_fd == -1)
        {
            throw std::runtime_error(std::string("signalfd: ") + strerror(errno));
        }

        event_loop loop;

        auto session_mgr = std::make_unique<session_manager>();
        auto timer_mgr = std::make_unique<timer_manager>(loop);

        server srv(loop, shm, response_efd, cfg, std::move(session_mgr), std::move(timer_mgr));

        // MQTT client — connects to broker and wires socket into the event loop.
        auto mqtt_db_pool = std::make_shared<connection_pool>(build_connection_string(), 2);
        mqtt_client mqtt(cfg, loop, mqtt_db_pool);
        srv.set_mqtt_client(&mqtt);
        if (mqtt.connect() != 0)
        {
            LOG_ERROR_MSG("[REACTOR] MQTT connect failed, continuing without MQTT");
            std::cerr << "[REACTOR] MQTT connect failed, continuing without MQTT\n";
        }

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

        srv.start();

        LOG_INFO_MSG("[REACTOR] Server running, waiting for connections");

        loop.run();

        LOG_INFO_MSG("[REACTOR] Event loop stopped, waiting for children");

        int status;
        waitpid(worker_pid, &status, 0);
        if (WIFEXITED(status))
        {
            LOG_INFO_MSG("[REACTOR] Worker exited status=%d", WEXITSTATUS(status));
        }

        waitpid(api_rest_pid, &status, 0);
        if (WIFEXITED(status))
        {
            LOG_INFO_MSG("[REACTOR] REST API exited status=%d", WEXITSTATUS(status));
        }

        close(sig_fd);
        close(response_efd);

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
