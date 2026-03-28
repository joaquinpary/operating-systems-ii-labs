#include "flow_solver.hpp"
#include <gtest/gtest.h>

namespace server
{
namespace
{

TEST(FlowSolverTest, SingleNodeTrivial)
{
    std::vector<std::vector<double>> graph = {{0.0}};
    EXPECT_DOUBLE_EQ(ford_fulkerson(graph, 0, 0), 0.0);
}

TEST(FlowSolverTest, DisconnectedNodes)
{
    std::vector<std::vector<double>> graph = {{0.0, 0.0}, {0.0, 0.0}};
    EXPECT_DOUBLE_EQ(ford_fulkerson(graph, 0, 1), 0.0);
}

TEST(FlowSolverTest, SinglePath)
{
    // 0 -> 1 -> 2
    // Minimum capacity is 5.0
    std::vector<std::vector<double>> graph = {{0.0, 10.0, 0.0}, {0.0, 0.0, 5.0}, {0.0, 0.0, 0.0}};
    EXPECT_DOUBLE_EQ(ford_fulkerson(graph, 0, 2), 5.0);
}

TEST(FlowSolverTest, ParallelPaths)
{
    // 0 -> 1 -> 3 (capacity 10)
    // 0 -> 2 -> 3 (capacity 5)
    // Total max flow should be 15
    std::vector<std::vector<double>> graph = {
        {0.0, 10.0, 5.0, 0.0}, {0.0, 0.0, 0.0, 10.0}, {0.0, 0.0, 0.0, 5.0}, {0.0, 0.0, 0.0, 0.0}};
    EXPECT_DOUBLE_EQ(ford_fulkerson(graph, 0, 3), 15.0);
}

TEST(FlowSolverTest, ClassicSixNode)
{
    // The example provided in the problem description
    std::vector<std::vector<double>> graph = {
        // 0     1     2     3     4     5
        {0.0, 16.5, 13.2, 0.0, 0.0, 0.0}, // 0
        {0.0, 0.0, 10.0, 12.0, 0.0, 0.0}, // 1
        {0.0, 4.0, 0.0, 0.0, 14.1, 0.0},  // 2
        {0.0, 0.0, 9.0, 0.0, 0.0, 20.0},  // 3
        {0.0, 0.0, 0.0, 7.0, 0.0, 4.5},   // 4
        {0.0, 0.0, 0.0, 0.0, 0.0, 0.0}    // 5
    };
    // From 0 to 5, the expected max flow is 23.5
    EXPECT_DOUBLE_EQ(ford_fulkerson(graph, 0, 5), 23.5);
}

} // namespace
} // namespace server
