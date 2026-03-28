#include "api_rest.hpp"

#include "api_parser.hpp"
#include "flow_solver.hpp"
#include "graph_builder.hpp"
#include <common/logger.h>
#include <httplib.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <stdexcept>
#include <thread>

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

        auto shared_graph = std::make_shared<server::GraphData>();
        auto graph_mutex = std::make_shared<std::mutex>();

        server.Post("/map", [shared_graph, graph_mutex](const httplib::Request& request, httplib::Response& response) {
            try
            {
                server::MapParseResult parsed_map = server::parse_map_json(request.body);
                server::GraphData graph = server::build_adjacency_matrix(parsed_map.nodes);

                {
                    std::lock_guard<std::mutex> lock(*graph_mutex);
                    *shared_graph = graph;
                }

                int matrix_size = static_cast<int>(graph.adj_matrix.size());

                for (int i = 0; i < matrix_size; ++i)
                {
                    std::string row_str;
                    for (int j = 0; j < matrix_size; ++j)
                    {
                        char val_buf[32];
                        std::snprintf(val_buf, sizeof(val_buf), "%8.2f", graph.adj_matrix[i][j]);
                        row_str += val_buf;
                    }
                    LOG_INFO_MSG("[API] adj_matrix[%d]: %s", i, row_str.c_str());
                }

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
                              R"({"status":"ok","accepted":%d,"discarded":%d,"matrix_size":%d,"nodes":%s})",
                              (int)parsed_map.nodes.size(), parsed_map.total_discarded, matrix_size,
                              accepted_ids.c_str());

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
            response.set_content(
                R"({"status":"ok","algorithms":["markets-path","fulfillment-flow","fulfillment-circuit"]})",
                "application/json");
        });

        server.Post("/request/fulfillment-flow", [shared_graph, graph_mutex](const httplib::Request& request,
                                                                             httplib::Response& response) {
            try
            {
                server::FlowRequest req = server::parse_flow_request_json(request.body);

                std::lock_guard<std::mutex> lock(*graph_mutex);
                if (shared_graph->node_to_index.empty() || shared_graph->adj_matrix.empty())
                {
                    throw std::runtime_error("No map data loaded. Call /map first.");
                }

                auto source_it = shared_graph->node_to_index.find(req.source);
                auto sink_it = shared_graph->node_to_index.find(req.sink);

                if (source_it == shared_graph->node_to_index.end())
                {
                    throw std::runtime_error("Source node not found in graph: " + req.source);
                }
                if (sink_it == shared_graph->node_to_index.end())
                {
                    throw std::runtime_error("Sink node not found in graph: " + req.sink);
                }

                int source_idx = source_it->second;
                int sink_idx = sink_it->second;

                double max_flow = server::ford_fulkerson(shared_graph->adj_matrix, source_idx, sink_idx);

                response.status = 200;
                response.set_header("Content-Type", "application/json");

                char res_buf[256];
                std::snprintf(res_buf, sizeof(res_buf), R"({"status":"ok","source":"%s","sink":"%s","max_flow":%.2f})",
                              req.source.c_str(), req.sink.c_str(), max_flow);

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

        server.Post("/request/fulfillment-circuit", [](const httplib::Request&, httplib::Response& response) {
            response.status = 200;
            response.set_header("Content-Type", "application/json");
            response.set_content(R"({"status":"ok","message":"dummy fulfillment-circuit"})", "application/json");
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
