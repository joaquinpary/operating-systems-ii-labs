#include "api_rest.hpp"

#include <common/logger.h>
#include <httplib.h>

#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <pthread.h>
#include <thread>
#include <stdexcept>

namespace
{
void install_signal_waiter(httplib::Server& server)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);

    std::thread([&server, mask]() mutable {
        int signal_number = 0;
        if (sigwait(&mask, &signal_number) == 0)
        {
            LOG_INFO_MSG("[API] Received signal %d, stopping REST server", signal_number);
            server.stop();
        }
    }).detach();
}
} // namespace

void run_api_rest_process(const config::server_config& cfg)
{
    const char* log_dir = std::getenv("LOG_DIR");
    if (log_dir == nullptr)
    {
        log_dir = "logs/server";
    }

    logger_config_t log_cfg = {
        .max_file_size = 50 * 1024 * 1024,
        .max_backup_files = 1000,
        .min_level = LOG_DEBUG,
    };
    std::snprintf(log_cfg.log_file_path, sizeof(log_cfg.log_file_path), "%s/server_api_rest.log", log_dir);
    log_init(&log_cfg);

    try
    {
        httplib::Server server;

        install_signal_waiter(server);

        server.Post("/map", [](const httplib::Request&, httplib::Response& response) {
            response.status = 200;
            response.set_header("Content-Type", "application/json");
            response.set_content(R"({"status":"ok","map_id":"dummy_map_1"})", "application/json");
        });

        server.Post("/request", [](const httplib::Request&, httplib::Response& response) {
            response.status = 200;
            response.set_header("Content-Type", "application/json");
            response.set_content(R"({"status":"ok","algorithms":["markets-path","fulfillment-flow","fulfillment-circuit"]})", "application/json");
        });

        server.Post("/fulfillment-circuit", [](const httplib::Request&, httplib::Response& response) {
            response.status = 200;
            response.set_header("Content-Type", "application/json");
            response.set_content(R"({"status":"ok","message":"dummy fulfillment-circuit"})", "application/json");
        });

        server.Post("/fulfillment-flow", [](const httplib::Request&, httplib::Response& response) {
            response.status = 200;
            response.set_header("Content-Type", "application/json");
            response.set_content(R"({"status":"ok","message":"dummy fulfillment-flow"})", "application/json");
        });

        auto get_results_handler = [](const httplib::Request&, httplib::Response& response) {
            response.status = 200;
            response.set_header("Content-Type", "application/json");
            response.set_content(R"({"status":"ok","results":[]})", "application/json");
        };
        server.Get("/results", get_results_handler);
        server.Get("/results/", get_results_handler);

        LOG_INFO_MSG("[API] REST server listening on port %u", static_cast<unsigned>(cfg.api_rest_port));
        if (!server.listen("0.0.0.0", static_cast<int>(cfg.api_rest_port)))
        {
            throw std::runtime_error("REST server failed to bind/listen");
        }
        LOG_INFO_MSG("[API] REST server stopped");
    }
    catch (const std::exception& ex)
    {
        LOG_ERROR_MSG("[API] Fatal: %s", ex.what());
    }

    log_close();
}
