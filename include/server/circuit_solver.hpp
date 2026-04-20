#ifndef CIRCUIT_SOLVER_HPP
#define CIRCUIT_SOLVER_HPP

#include <vector>

namespace server
{

using SymPath = std::vector<int>;
using SymPaths = std::vector<SymPath>;
using SymMatrix = std::vector<std::vector<SymPaths>>;

struct CircuitResult
{
    bool has_circuit;
    std::vector<SymPath> circuits;
    double execution_time_ms;
};

SymMatrix build_symbolic_matrix(const std::vector<std::vector<double>>& adj_matrix);
SymMatrix symbolic_matrix_multiply(const SymMatrix& left, const SymMatrix& right);
void remove_repeated_node_paths(SymMatrix& matrix);
CircuitResult find_hamiltonian_circuits(const std::vector<std::vector<double>>& adj_matrix, int start_node = -1);

} // namespace server

#endif // CIRCUIT_SOLVER_HPP
