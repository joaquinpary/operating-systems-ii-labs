#ifndef GRAPH_BUILDER_HPP
#define GRAPH_BUILDER_HPP

#include "api_parser.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace server
{

// --- Connection type identifiers ---
inline constexpr const char* CONN_TYPE_ROAD = "road";
inline constexpr const char* CONN_TYPE_RAIL = "rail";
inline constexpr const char* CONN_TYPE_TRAIL = "trail";
inline constexpr const char* CONN_TYPE_TUNNEL = "tunnel";
inline constexpr const char* CONN_TYPE_BRIDGE = "bridge";
inline constexpr const char* CONN_TYPE_WATERWAY = "waterway";
inline constexpr const char* CONN_TYPE_DRONE = "drone";
inline constexpr const char* CONN_TYPE_BLOCKED = "blocked";
inline constexpr const char* CONN_TYPE_MANUAL = "manual";

// --- Connection type base modifiers ---
inline constexpr double MOD_ROAD = 1.0;
inline constexpr double MOD_RAIL = 0.7;
inline constexpr double MOD_TRAIL = 1.3;
inline constexpr double MOD_TUNNEL = 1.1;
inline constexpr double MOD_BRIDGE = 1.4;
inline constexpr double MOD_WATERWAY = 0.9;
inline constexpr double MOD_DRONE = 1.2;
inline constexpr double MOD_MANUAL = 1.6;
inline constexpr double MOD_DEFAULT = 1.0;

// --- Environmental condition identifiers ---
inline constexpr const char* COND_INFECTED_ACTIVITY = "infected_activity";
inline constexpr const char* COND_WEATHER_RAIN = "weather_rain";
inline constexpr const char* COND_FOGGY_VISIBILITY = "foggy_visibility";
inline constexpr const char* COND_CLEARED = "cleared";
inline constexpr const char* COND_REINFORCED = "reinforced";

// --- Environmental condition modifiers ---
inline constexpr double COND_MOD_INFECTED_ACTIVITY = 0.3;
inline constexpr double COND_MOD_WEATHER_RAIN = 0.2;
inline constexpr double COND_MOD_FOGGY_VISIBILITY = 0.1;
inline constexpr double COND_MOD_CLEARED = -0.2;
inline constexpr double COND_MOD_REINFORCED = -0.3;

// --- Formula constants ---
inline constexpr double CONDITION_BASE = 1.0;
inline constexpr double BLOCKED_COST = 0.0;
inline constexpr double NO_CONNECTION = 0.0;

struct GraphData
{
    std::unordered_map<std::string, int> node_to_index;
    std::vector<std::vector<double>> adj_matrix;
};

/**
 * Calculates the cost of traversing an edge using the formula:
 * base_weight * type_modifier * (1 + sum(condition_modifiers))
 *
 * Returns 0.0 for blocked connections (meaning: do not add this edge).
 *
 * @param edge The map edge to evaluate
 * @return The computed cost, or 0.0 if the edge is blocked
 */
double calculate_edge_cost(const MapEdge& edge);

/**
 * Builds a directed adjacency matrix from filtered map nodes.
 *
 * 1. Assigns each node a numeric index (0..N-1)
 * 2. Creates an NxN matrix initialized to 0.0
 * 3. For each connection, computes cost and sets adj_matrix[u][v]
 *    (skips edges to nodes not in the valid set, and blocked edges)
 *
 * @param nodes The filtered vector of valid MapNode structs
 * @return GraphData containing the index mapping and adjacency matrix
 */
GraphData build_adjacency_matrix(const std::vector<MapNode>& nodes);

} // namespace server

#endif // GRAPH_BUILDER_HPP
