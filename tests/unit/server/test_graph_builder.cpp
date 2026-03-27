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

} // namespace
} // namespace server
