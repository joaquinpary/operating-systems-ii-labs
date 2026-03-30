#ifndef RESULT_STORE_HPP
#define RESULT_STORE_HPP

#include "circuit_solver.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace server
{

void init_result_store(const std::string& mongo_uri, const std::string& database_name);

void save_flow_result(const std::string& source,
                      const std::string& sink,
                      double max_flow,
                      double execution_time_ms,
                      std::int64_t timestamp_ms);

void save_circuit_result(const std::string& start,
                         const std::vector<std::string>& subgraph_node_ids,
                         const CircuitResult& circuit_result,
                         std::int64_t timestamp_ms);

std::string get_all_results();

} // namespace server

#endif // RESULT_STORE_HPP