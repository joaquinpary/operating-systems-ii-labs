#include <gtest/gtest.h>
#include "graph_builder.hpp"

namespace server
{
namespace
{

// --- Edge cost calculation tests ---

TEST(GraphBuilderTest, EdgeCostRoadNoConditions)
{
    MapEdge edge;
    edge.connection_type = CONN_TYPE_ROAD;
    edge.base_weight = 50.0;

    EXPECT_DOUBLE_EQ(calculate_edge_cost(edge), 50.0);
}

TEST(GraphBuilderTest, EdgeCostRailWithInfected)
{
    MapEdge edge;
    edge.connection_type = CONN_TYPE_RAIL;
    edge.base_weight = 100.0;
    edge.connection_conditions = {COND_INFECTED_ACTIVITY};

    // 100 * 0.7 * (1 + 0.3) = 100 * 0.7 * 1.3 = 91.0
    EXPECT_DOUBLE_EQ(calculate_edge_cost(edge), 91.0);
}

TEST(GraphBuilderTest, EdgeCostTrailMultipleConditions)
{
    MapEdge edge;
    edge.connection_type = CONN_TYPE_TRAIL;
    edge.base_weight = 80.0;
    edge.connection_conditions = {COND_WEATHER_RAIN, COND_CLEARED};

    // 80 * 1.3 * (1 + 0.2 - 0.2) = 80 * 1.3 * 1.0 = 104.0
    EXPECT_DOUBLE_EQ(calculate_edge_cost(edge), 104.0);
}

TEST(GraphBuilderTest, EdgeCostBlocked)
{
    MapEdge edge;
    edge.connection_type = CONN_TYPE_BLOCKED;
    edge.base_weight = 999.0;
    edge.connection_conditions = {COND_INFECTED_ACTIVITY};

    EXPECT_DOUBLE_EQ(calculate_edge_cost(edge), BLOCKED_COST);
}

TEST(GraphBuilderTest, EdgeCostUnknownType)
{
    MapEdge edge;
    edge.connection_type = "teleporter";
    edge.base_weight = 40.0;

    // Unknown type defaults to MOD_DEFAULT
    EXPECT_DOUBLE_EQ(calculate_edge_cost(edge), 40.0);
}

TEST(GraphBuilderTest, EdgeCostUnknownCondition)
{
    MapEdge edge;
    edge.connection_type = CONN_TYPE_ROAD;
    edge.base_weight = 50.0;
    edge.connection_conditions = {"alien_invasion"};

    // Unknown condition is ignored: 50 * 1.0 * (1 + 0) = 50.0
    EXPECT_DOUBLE_EQ(calculate_edge_cost(edge), 50.0);
}

// --- Adjacency matrix construction tests ---

static MapNode make_node(const std::string& id, const std::vector<MapEdge>& connections = {})
{
    MapNode node;
    node.node_id = id;
    node.node_type = "fulfillment_center";
    node.is_secure = true;
    node.is_active = true;
    node.connections = connections;
    return node;
}

static MapEdge make_edge(const std::string& to, const std::string& type, double weight,
                         const std::vector<std::string>& conditions = {})
{
    MapEdge edge;
    edge.to = to;
    edge.connection_type = type;
    edge.base_weight = weight;
    edge.connection_conditions = conditions;
    return edge;
}

TEST(GraphBuilderTest, BuildMatrixEmptyNodes)
{
    std::vector<MapNode> nodes;
    GraphData data = build_adjacency_matrix(nodes);

    EXPECT_TRUE(data.node_to_index.empty());
    EXPECT_TRUE(data.adj_matrix.empty());
}

TEST(GraphBuilderTest, BuildMatrixDimensions)
{
    std::vector<MapNode> nodes = {make_node("A"), make_node("B"), make_node("C")};
    GraphData data = build_adjacency_matrix(nodes);

    ASSERT_EQ(data.adj_matrix.size(), 3);
    for (const auto& row : data.adj_matrix)
    {
        EXPECT_EQ(row.size(), 3);
    }
}

TEST(GraphBuilderTest, NodeToIndexMapping)
{
    std::vector<MapNode> nodes = {make_node("N001"), make_node("N042"), make_node("M003")};
    GraphData data = build_adjacency_matrix(nodes);

    ASSERT_EQ(data.node_to_index.size(), 3);
    EXPECT_EQ(data.node_to_index.at("N001"), 0);
    EXPECT_EQ(data.node_to_index.at("N042"), 1);
    EXPECT_EQ(data.node_to_index.at("M003"), 2);
}

TEST(GraphBuilderTest, BuildMatrixDirected)
{
    // A -> B with road, weight 50. B has no connection back to A.
    std::vector<MapNode> nodes = {
        make_node("A", {make_edge("B", CONN_TYPE_ROAD, 50.0)}),
        make_node("B"),
    };
    GraphData data = build_adjacency_matrix(nodes);

    int a = data.node_to_index.at("A");
    int b = data.node_to_index.at("B");

    // A->B exists: 50 * 1.0 * 1.0 = 50.0
    EXPECT_DOUBLE_EQ(data.adj_matrix[a][b], 50.0);
    // B->A must NOT exist (directed graph)
    EXPECT_DOUBLE_EQ(data.adj_matrix[b][a], NO_CONNECTION);
}

TEST(GraphBuilderTest, BuildMatrixSkipsInvalidTarget)
{
    // A connects to "GHOST" which is not in the valid nodes
    std::vector<MapNode> nodes = {
        make_node("A", {make_edge("GHOST", CONN_TYPE_ROAD, 50.0)}),
        make_node("B"),
    };
    GraphData data = build_adjacency_matrix(nodes);

    // All cells should be 0 since the only edge points to a discarded node
    for (const auto& row : data.adj_matrix)
    {
        for (double val : row)
        {
            EXPECT_DOUBLE_EQ(val, NO_CONNECTION);
        }
    }
}

TEST(GraphBuilderTest, BuildMatrixSkipsBlocked)
{
    std::vector<MapNode> nodes = {
        make_node("A", {make_edge("B", CONN_TYPE_BLOCKED, 100.0)}),
        make_node("B"),
    };
    GraphData data = build_adjacency_matrix(nodes);

    int a = data.node_to_index.at("A");
    int b = data.node_to_index.at("B");

    EXPECT_DOUBLE_EQ(data.adj_matrix[a][b], NO_CONNECTION);
}

TEST(GraphBuilderTest, BuildMatrixMultipleEdges)
{
    // A -> B (road, 50), A -> C (rail, 100, infected), B -> C (trail, 80)
    std::vector<MapNode> nodes = {
        make_node("A", {
            make_edge("B", CONN_TYPE_ROAD, 50.0),
            make_edge("C", CONN_TYPE_RAIL, 100.0, {COND_INFECTED_ACTIVITY}),
        }),
        make_node("B", {make_edge("C", CONN_TYPE_TRAIL, 80.0)}),
        make_node("C"),
    };
    GraphData data = build_adjacency_matrix(nodes);

    int a = data.node_to_index.at("A");
    int b = data.node_to_index.at("B");
    int c = data.node_to_index.at("C");

    // A->B: 50 * 1.0 * 1.0 = 50.0
    EXPECT_DOUBLE_EQ(data.adj_matrix[a][b], 50.0);
    // A->C: 100 * 0.7 * (1 + 0.3) = 91.0
    EXPECT_DOUBLE_EQ(data.adj_matrix[a][c], 91.0);
    // B->C: 80 * 1.3 * 1.0 = 104.0
    EXPECT_DOUBLE_EQ(data.adj_matrix[b][c], 104.0);
    // No reverse edges
    EXPECT_DOUBLE_EQ(data.adj_matrix[b][a], NO_CONNECTION);
    EXPECT_DOUBLE_EQ(data.adj_matrix[c][a], NO_CONNECTION);
    EXPECT_DOUBLE_EQ(data.adj_matrix[c][b], NO_CONNECTION);
}

} // namespace
} // namespace server
