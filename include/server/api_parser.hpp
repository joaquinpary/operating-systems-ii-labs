#ifndef API_PARSER_HPP
#define API_PARSER_HPP

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

} // namespace server

#endif // API_PARSER_HPP
