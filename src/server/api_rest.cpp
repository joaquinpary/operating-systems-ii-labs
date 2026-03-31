#include "api_rest.hpp"

#include "api_parser.hpp"
#include "circuit_solver.hpp"
#include "flow_solver.hpp"
#include "graph_builder.hpp"
#include "result_store.hpp"
#include <common/logger.h>
#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr const char* FULFILLMENT_CENTER_NODE_TYPE = "fulfillment_center";
constexpr size_t MAX_FULFILLMENT_CIRCUIT_NODES = 20;

#ifdef USE_OPENMP_FLOW
constexpr bool kUseOpenMPFlow = true;
#else
constexpr bool kUseOpenMPFlow = false;
#endif

#ifdef USE_OPENMP_CIRCUIT
constexpr bool kUseOpenMPCircuit = true;
#else
constexpr bool kUseOpenMPCircuit = false;
#endif

std::int64_t current_timestamp_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

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

bool is_graph_loaded(const server::GraphData& graph)
{
    return !graph.node_to_index.empty() && !graph.adj_matrix.empty();
}

std::vector<int> collect_node_indices_by_type(const server::GraphData& graph, const std::string& node_type)
{
    std::vector<int> indices;
    indices.reserve(graph.index_to_node_id.size());

    for (size_t index = 0; index < graph.index_to_node_id.size(); ++index)
    {
        const std::string& node_id = graph.index_to_node_id[index];
        auto type_it = graph.node_id_to_type.find(node_id);
        if (type_it != graph.node_id_to_type.end() && type_it->second == node_type)
        {
            indices.push_back(static_cast<int>(index));
        }
    }

    return indices;
}

std::vector<std::vector<double>> build_binary_subgraph(const server::GraphData& graph,
                                                       const std::vector<int>& selected_indices)
{
    std::vector<std::vector<double>> subgraph(selected_indices.size(),
                                              std::vector<double>(selected_indices.size(), 0.0));

    for (size_t row = 0; row < selected_indices.size(); ++row)
    {
        for (size_t column = 0; column < selected_indices.size(); ++column)
        {
            if (graph.adj_matrix[static_cast<size_t>(selected_indices[row])]
                                [static_cast<size_t>(selected_indices[column])] > 0.0)
            {
                subgraph[row][column] = 1.0;
            }
        }
    }

    return subgraph;
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
        server::init_result_store(
            config::get_env_var("MONGO_URI",
                                "mongodb://mongo:27017/?serverSelectionTimeoutMS=1000&connectTimeoutMS=1000"),
            config::get_env_var("MONGO_DB", "dhl_results"));

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

                std::string res_str = R"({"status":"ok","accepted":)" + std::to_string(parsed_map.nodes.size()) + 
                                      R"(,"discarded":)" + std::to_string(parsed_map.total_discarded) + 
                                      R"(,"matrix_size":)" + std::to_string(matrix_size) + 
                                      R"(,"nodes":)" + accepted_ids + "}";

                response.set_content(res_str, "application/json");
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
                server::FlowResult flow_res;
                int node_count = 0;

                {
                    std::lock_guard<std::mutex> lock(*graph_mutex);
                    if (!is_graph_loaded(*shared_graph))
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
                    node_count = static_cast<int>(shared_graph->adj_matrix.size());

                    flow_res = server::ford_fulkerson(shared_graph->adj_matrix, source_idx, sink_idx);
                }

                const std::int64_t timestamp_ms = current_timestamp_ms();
                const std::string timestamp = server::format_timestamp_iso(timestamp_ms);
                server::save_flow_result(req.source,
                                         req.sink,
                                         node_count,
                                         flow_res.max_flow,
                                         flow_res.execution_time_ms,
                                         timestamp,
                                         kUseOpenMPFlow);

                response.status = 200;
                response.set_header("Content-Type", "application/json");

                char res_buf[512];
                std::snprintf(res_buf, sizeof(res_buf),
                              R"({"status":"ok","source":"%s","sink":"%s","node_count":%d,"max_flow":%.2f,"execution_time_ms":%.2f,"use_openmp":%s,"timestamp":"%s"})",
                              req.source.c_str(), req.sink.c_str(), node_count, flow_res.max_flow,
                              flow_res.execution_time_ms, kUseOpenMPFlow ? "true" : "false", timestamp.c_str());

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

        server.Post("/request/fulfillment-circuit", [shared_graph, graph_mutex](const httplib::Request& request,
                                                                                httplib::Response& response) {
            try
            {
                server::CircuitRequest req = server::parse_circuit_request_json(request.body);
                std::vector<std::string> subgraph_node_ids;
                server::CircuitResult circuit_result;

                {
                    std::lock_guard<std::mutex> lock(*graph_mutex);
                    if (!is_graph_loaded(*shared_graph))
                    {
                        throw std::runtime_error("No map data loaded. Call /map first.");
                    }

                    std::vector<int> fulfillment_indices =
                        collect_node_indices_by_type(*shared_graph, FULFILLMENT_CENTER_NODE_TYPE);
                    if (fulfillment_indices.size() > MAX_FULFILLMENT_CIRCUIT_NODES)
                    {
                        throw std::runtime_error("Fulfillment-center subgraph exceeds the supported limit of "
                                                 + std::to_string(MAX_FULFILLMENT_CIRCUIT_NODES) + " nodes");
                    }

                    subgraph_node_ids.reserve(fulfillment_indices.size());
                    for (int index : fulfillment_indices)
                    {
                        subgraph_node_ids.push_back(shared_graph->index_to_node_id.at(static_cast<size_t>(index)));
                    }

                    int start_index = -1;
                    if (!req.start.empty())
                    {
                        auto start_it = std::find(subgraph_node_ids.begin(), subgraph_node_ids.end(), req.start);
                        if (start_it == subgraph_node_ids.end())
                        {
                            throw std::runtime_error("Start node is not a secure active fulfillment center: " + req.start);
                        }

                        start_index = static_cast<int>(std::distance(subgraph_node_ids.begin(), start_it));
                    }

                    const std::vector<std::vector<double>> fulfillment_subgraph =
                        build_binary_subgraph(*shared_graph, fulfillment_indices);
                    circuit_result = server::find_hamiltonian_circuits(fulfillment_subgraph, start_index);
                }

                const std::int64_t timestamp_ms = current_timestamp_ms();
                const std::string timestamp = server::format_timestamp_iso(timestamp_ms);
                server::save_circuit_result(req.start, subgraph_node_ids, circuit_result, timestamp, kUseOpenMPCircuit);

                response.status = 200;
                response.set_header("Content-Type", "application/json");
                response.set_content(
                    server::build_circuit_response_json(subgraph_node_ids, circuit_result, timestamp, kUseOpenMPCircuit),
                    "application/json");
            }
            catch (const std::exception& e)
            {
                response.status = 400;
                response.set_header("Content-Type", "application/json");
                std::string err_json = R"({"status":"error","message":")" + std::string(e.what()) + R"("})";
                response.set_content(err_json, "application/json");
            }
        });

        auto get_results_handler = [](const httplib::Request&, httplib::Response& response) {
            response.status = 200;
            response.set_header("Content-Type", "application/json");
            response.set_content(server::get_all_results(), "application/json");
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
