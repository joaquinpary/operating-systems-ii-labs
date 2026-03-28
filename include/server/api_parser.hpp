#ifndef API_PARSER_HPP
#define API_PARSER_HPP

#include "circuit_solver.hpp"

#include <string>
#include <vector>

namespace server
{

struct MapNodeLocation
{
    double latitude;
    double longitude;
};

struct MapEdge
{
    std::string to;
    std::string connection_type;
    double base_weight;
    std::vector<std::string> connection_conditions;
};

struct MapNode
{
    std::string node_id;
    std::string node_name;
    std::string node_type;
    std::string node_description;
    MapNodeLocation location;
    std::vector<std::string> node_tags;
    bool is_secure;
    bool is_active;
    std::vector<MapEdge> connections;
};

struct MapParseResult
{
    std::vector<MapNode> nodes;
    int total_received;
    int total_discarded;
};

/**
 * Parses a JSON array of map nodes, verifying security and activation.
 * Keeps only nodes of type "fulfillment_center" and "market" that are
 * both secure and active.
 *
 * @param json_body The payload string received at POST /map
 * @return Struct containing the accepted nodes, total items, and discarded items
 * @throws std::runtime_error If the payload is not valid JSON
 */
MapParseResult parse_map_json(const std::string& json_body);

struct FlowRequest
{
    std::string source;
    std::string sink;
};

/**
 * Parses a JSON object containing a source and a sink node ID.
 *
 * @param json_body The payload string received at POST /request/fulfillment-flow
 * @return Struct containing the parsed source and sink strings
 * @throws std::runtime_error If the payload is not valid JSON or missing fields
 */
FlowRequest parse_flow_request_json(const std::string& json_body);

struct CircuitRequest
{
    std::string start;
};

/**
 * Parses a JSON object containing an optional start node ID.
 *
 * Empty payloads are accepted and interpreted as no preferred start node.
 *
 * @param json_body The payload string received at POST /request/fulfillment-circuit
 * @return Struct containing the optional start node string
 * @throws std::runtime_error If the payload is not valid JSON or the start field is invalid
 */
CircuitRequest parse_circuit_request_json(const std::string& json_body);

/**
 * Serializes a circuit result into a JSON response string.
 *
 * @param subgraph_node_ids Ordered node IDs matching the subgraph indices
 * @param circuit_result The result returned by find_hamiltonian_circuits
 * @return JSON string with status, has_circuit, node_count, and circuits
 */
std::string build_circuit_response_json(const std::vector<std::string>& subgraph_node_ids,
                                        const CircuitResult& circuit_result);

} // namespace server

#endif // API_PARSER_HPP
