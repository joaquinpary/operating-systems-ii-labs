#ifndef RESULT_STORE_HPP
#define RESULT_STORE_HPP

#include "circuit_solver.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace server
{

std::string format_timestamp_iso(std::int64_t timestamp_ms);

void init_result_store(const std::string& mongo_uri, const std::string& database_name);

void save_flow_result(const std::string& source, const std::string& sink, int node_count, double max_flow,
                      double execution_time_ms, const std::string& timestamp, bool use_openmp);

void save_circuit_result(const std::string& start, const std::vector<std::string>& subgraph_node_ids,
                         const CircuitResult& circuit_result, const std::string& timestamp, bool use_openmp);

std::string get_all_results();

} // namespace server

#endif // RESULT_STORE_HPP
