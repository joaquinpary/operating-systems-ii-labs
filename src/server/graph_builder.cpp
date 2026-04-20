#include "graph_builder.hpp"

#include <string>
#include <unordered_map>

namespace server
{

static const std::unordered_map<std::string, double> type_modifiers = {
    {CONN_TYPE_ROAD, MOD_ROAD},     {CONN_TYPE_RAIL, MOD_RAIL},     {CONN_TYPE_TRAIL, MOD_TRAIL},
    {CONN_TYPE_TUNNEL, MOD_TUNNEL}, {CONN_TYPE_BRIDGE, MOD_BRIDGE}, {CONN_TYPE_WATERWAY, MOD_WATERWAY},
    {CONN_TYPE_DRONE, MOD_DRONE},   {CONN_TYPE_MANUAL, MOD_MANUAL},
};

static const std::unordered_map<std::string, double> condition_modifiers = {
    {COND_INFECTED_ACTIVITY, COND_MOD_INFECTED_ACTIVITY},
    {COND_WEATHER_RAIN, COND_MOD_WEATHER_RAIN},
    {COND_FOGGY_VISIBILITY, COND_MOD_FOGGY_VISIBILITY},
    {COND_CLEARED, COND_MOD_CLEARED},
    {COND_REINFORCED, COND_MOD_REINFORCED},
};

double calculate_edge_cost(const MapEdge& edge)
{
    if (edge.connection_type == CONN_TYPE_BLOCKED)
    {
        return BLOCKED_COST;
    }

    double type_mod = MOD_DEFAULT;
    auto type_it = type_modifiers.find(edge.connection_type);
    if (type_it != type_modifiers.end())
    {
        type_mod = type_it->second;
    }

    double sum_conditions = 0;
    for (const auto& condition : edge.connection_conditions)
    {
        auto cond_it = condition_modifiers.find(condition);
        if (cond_it != condition_modifiers.end())
        {
            sum_conditions += cond_it->second;
        }
    }

    return edge.base_weight * type_mod * (CONDITION_BASE + sum_conditions);
}

GraphData build_adjacency_matrix(const std::vector<MapNode>& nodes)
{
    GraphData data;

    int n = static_cast<int>(nodes.size());
    data.index_to_node_id.resize(static_cast<size_t>(n));

    int index = 0;
    for (const auto& node : nodes)
    {
        data.node_to_index[node.node_id] = index;
        data.index_to_node_id[static_cast<size_t>(index)] = node.node_id;
        data.node_id_to_type[node.node_id] = node.node_type;
        ++index;
    }

    data.adj_matrix.assign(n, std::vector<double>(n, NO_CONNECTION));

    for (const auto& node : nodes)
    {
        int source_idx = data.node_to_index[node.node_id];
        for (const auto& conn : node.connections)
        {
            auto target_it = data.node_to_index.find(conn.to);
            if (target_it == data.node_to_index.end())
            {
                continue;
            }

            double cost = calculate_edge_cost(conn);
            if (cost > NO_CONNECTION)
            {
                data.adj_matrix[source_idx][target_it->second] = cost;
            }
        }
    }

    return data;
}

} // namespace server
