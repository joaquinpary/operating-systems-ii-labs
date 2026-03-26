#include "api_rest.hpp"

#include <common/logger.h>
#include "map_parser.hpp"
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

        server.Post("/map", [](const httplib::Request& request, httplib::Response& response) {
            try
            {
                server::MapParseResult parsed_map = server::parse_map_json(request.body);
                
                std::string accepted_ids = "[";
                for (size_t i = 0; i < parsed_map.nodes.size(); ++i)
                {
                    accepted_ids += "\"" + parsed_map.nodes[i].node_id + "\"";
                    if (i < parsed_map.nodes.size() - 1)
                        accepted_ids += ", ";
                }
                accepted_ids += "]";

                response.status = 200;
                response.set_header("Content-Type", "application/json");
                
                char res_buf[512];
                std::snprintf(res_buf, sizeof(res_buf),
                    R"({"status":"ok","accepted":%d,"discarded":%d,"nodes":%s})",
                    (int)parsed_map.nodes.size(), parsed_map.total_discarded, accepted_ids.c_str());
                
                response.set_content(res_buf, "application/json");
            }
            catch (const std::exception& e)
            {
                response.status = 400;
                response.set_header("Content-Type", "application/json");
                std::string err_json = R"({"status":"error","message":")" + std::string(e.what()) + R"("})";
                response.set_content(err_json, "application/json");
            }
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
