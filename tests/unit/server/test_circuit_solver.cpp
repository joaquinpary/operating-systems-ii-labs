#include "circuit_solver.hpp"

#include <gtest/gtest.h>
#include <stdexcept>

namespace server
{
namespace
{

TEST(CircuitSolverTest, BuildSymbolicMatrixEmptyGraph)
{
    SymMatrix matrix = build_symbolic_matrix({});
    EXPECT_TRUE(matrix.empty());
}

TEST(CircuitSolverTest, BuildSymbolicMatrixSingleEdge)
{
    std::vector<std::vector<double>> graph = {{0.0, 1.0}, {0.0, 0.0}};
    SymMatrix matrix = build_symbolic_matrix(graph);

    ASSERT_EQ(matrix.size(), 2);
    ASSERT_EQ(matrix[0][1].size(), 1);
    EXPECT_EQ(matrix[0][1][0], (SymPath{0, 1}));
    EXPECT_TRUE(matrix[1][0].empty());
}

TEST(CircuitSolverTest, SymbolicMultiplyBuildsLongerPath)
{
    std::vector<std::vector<double>> graph = {
        {0.0, 1.0, 0.0},
        {0.0, 0.0, 1.0},
        {0.0, 0.0, 0.0},
    };

    SymMatrix base = build_symbolic_matrix(graph);
    SymMatrix result = symbolic_matrix_multiply(base, base);

    ASSERT_EQ(result[0][2].size(), 1);
    EXPECT_EQ(result[0][2][0], (SymPath{0, 1, 2}));
}

TEST(CircuitSolverTest, RemoveRepeatedNodePathsFiltersInvalidPaths)
{
    SymMatrix matrix(1, std::vector<SymPaths>(1));
    matrix[0][0] = {{0, 1, 0}, {0, 1, 2}};

    remove_repeated_node_paths(matrix);

    ASSERT_EQ(matrix[0][0].size(), 1);
    EXPECT_EQ(matrix[0][0][0], (SymPath{0, 1, 2}));
}

TEST(CircuitSolverTest, HamiltonianCircuitTriangleDirected)
{
    std::vector<std::vector<double>> graph = {
        {0.0, 1.0, 0.0},
        {0.0, 0.0, 1.0},
        {1.0, 0.0, 0.0},
    };

    CircuitResult result = find_hamiltonian_circuits(graph);

    EXPECT_TRUE(result.has_circuit);
    ASSERT_EQ(result.circuits.size(), 1);
    EXPECT_EQ(result.circuits[0], (SymPath{0, 1, 2, 0}));
}

TEST(CircuitSolverTest, HamiltonianCircuitNoCircuit)
{
    std::vector<std::vector<double>> graph = {
        {0.0, 1.0, 0.0},
        {0.0, 0.0, 1.0},
        {0.0, 0.0, 0.0},
    };

    CircuitResult result = find_hamiltonian_circuits(graph);

    EXPECT_FALSE(result.has_circuit);
    EXPECT_TRUE(result.circuits.empty());
}

TEST(CircuitSolverTest, HamiltonianCircuitCompleteFourNodeDigraph)
{
    std::vector<std::vector<double>> graph = {
        {0.0, 1.0, 1.0, 1.0},
        {1.0, 0.0, 1.0, 1.0},
        {1.0, 1.0, 0.0, 1.0},
        {1.0, 1.0, 1.0, 0.0},
    };

    CircuitResult result = find_hamiltonian_circuits(graph);

    EXPECT_TRUE(result.has_circuit);
    EXPECT_EQ(result.circuits.size(), 6);
}

TEST(CircuitSolverTest, HamiltonianCircuitSupportsStartNode)
{
    std::vector<std::vector<double>> graph = {
        {0.0, 1.0, 0.0},
        {0.0, 0.0, 1.0},
        {1.0, 0.0, 0.0},
    };

    CircuitResult result = find_hamiltonian_circuits(graph, 1);

    EXPECT_TRUE(result.has_circuit);
    ASSERT_EQ(result.circuits.size(), 1);
    EXPECT_EQ(result.circuits[0], (SymPath{1, 2, 0, 1}));
}

TEST(CircuitSolverTest, HamiltonianCircuitTwoNodes)
{
    std::vector<std::vector<double>> graph = {
        {0.0, 1.0},
        {1.0, 0.0},
    };

    CircuitResult result = find_hamiltonian_circuits(graph);

    EXPECT_TRUE(result.has_circuit);
    ASSERT_EQ(result.circuits.size(), 1);
    EXPECT_EQ(result.circuits[0], (SymPath{0, 1, 0}));
}

TEST(CircuitSolverTest, HamiltonianCircuitDisconnectedGraph)
{
    std::vector<std::vector<double>> graph = {
        {0.0, 1.0, 0.0, 0.0},
        {0.0, 0.0, 1.0, 0.0},
        {0.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 0.0, 0.0},
    };

    CircuitResult result = find_hamiltonian_circuits(graph);

    EXPECT_FALSE(result.has_circuit);
    EXPECT_TRUE(result.circuits.empty());
}

TEST(CircuitSolverTest, HamiltonianCircuitRejectsInvalidStartIndex)
{
    std::vector<std::vector<double>> graph = {
        {0.0, 1.0},
        {1.0, 0.0},
    };

    EXPECT_THROW(find_hamiltonian_circuits(graph, 2), std::invalid_argument);
}

} // namespace
} // namespace server
