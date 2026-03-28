#include "circuit_solver.hpp"

#include <stdexcept>
#include <string>
#include <unordered_set>

namespace server
{
namespace
{

void validate_square_numeric_matrix(const std::vector<std::vector<double>>& matrix)
{
    const size_t size = matrix.size();
    for (const auto& row : matrix)
    {
        if (row.size() != size)
        {
            throw std::invalid_argument("Adjacency matrix must be square");
        }
    }
}

void validate_square_symbolic_matrix(const SymMatrix& matrix)
{
    const size_t size = matrix.size();
    for (const auto& row : matrix)
    {
        if (row.size() != size)
        {
            throw std::invalid_argument("Symbolic matrix must be square");
        }
    }
}

bool path_has_repeated_nodes(const SymPath& path)
{
    std::unordered_set<int> seen;
    for (int node : path)
    {
        if (!seen.insert(node).second)
        {
            return true;
        }
    }

    return false;
}

std::string path_key(const SymPath& path)
{
    std::string key;
    for (size_t index = 0; index < path.size(); ++index)
    {
        if (index > 0)
        {
            key += ',';
        }

        key += std::to_string(path[index]);
    }

    return key;
}

void deduplicate_paths(SymPaths& paths)
{
    std::unordered_set<std::string> seen;
    SymPaths unique_paths;
    unique_paths.reserve(paths.size());

    for (const auto& path : paths)
    {
        if (seen.insert(path_key(path)).second)
        {
            unique_paths.push_back(path);
        }
    }

    paths.swap(unique_paths);
}

SymPath canonicalize_circuit(const SymPath& circuit)
{
    if (circuit.empty())
    {
        return circuit;
    }

    SymPath open_circuit = circuit;
    if (open_circuit.size() > 1 && open_circuit.front() == open_circuit.back())
    {
        open_circuit.pop_back();
    }

    if (open_circuit.empty())
    {
        return circuit;
    }

    SymPath best_rotation = open_circuit;
    for (size_t offset = 1; offset < open_circuit.size(); ++offset)
    {
        SymPath candidate;
        candidate.reserve(open_circuit.size());

        for (size_t index = 0; index < open_circuit.size(); ++index)
        {
            candidate.push_back(open_circuit[(offset + index) % open_circuit.size()]);
        }

        if (candidate < best_rotation)
        {
            best_rotation = candidate;
        }
    }

    best_rotation.push_back(best_rotation.front());
    return best_rotation;
}

} // namespace

SymMatrix build_symbolic_matrix(const std::vector<std::vector<double>>& adj_matrix)
{
    validate_square_numeric_matrix(adj_matrix);

    const size_t size = adj_matrix.size();
    SymMatrix matrix(size, std::vector<SymPaths>(size));

    for (size_t row = 0; row < size; ++row)
    {
        for (size_t column = 0; column < size; ++column)
        {
            if (row != column && adj_matrix[row][column] > 0.0)
            {
                matrix[row][column].push_back(
                    {static_cast<int>(row), static_cast<int>(column)});
            }
        }
    }

    return matrix;
}

SymMatrix symbolic_matrix_multiply(const SymMatrix& left, const SymMatrix& right)
{
    validate_square_symbolic_matrix(left);
    validate_square_symbolic_matrix(right);

    if (left.size() != right.size())
    {
        throw std::invalid_argument("Symbolic matrices must have the same dimensions");
    }

    const size_t size = left.size();
    SymMatrix result(size, std::vector<SymPaths>(size));

    for (size_t row = 0; row < size; ++row)
    {
        for (size_t column = 0; column < size; ++column)
        {
            SymPaths cell_paths;

            for (size_t intermediate = 0; intermediate < size; ++intermediate)
            {
                const SymPaths& left_paths = left[row][intermediate];
                const SymPaths& right_paths = right[intermediate][column];

                if (left_paths.empty() || right_paths.empty())
                {
                    continue;
                }

                for (const auto& left_path : left_paths)
                {
                    for (const auto& right_path : right_paths)
                    {
                        if (left_path.empty() || right_path.empty() || left_path.back() != right_path.front())
                        {
                            continue;
                        }

                        SymPath combined = left_path;
                        combined.insert(combined.end(), right_path.begin() + 1, right_path.end());

                        if (!path_has_repeated_nodes(combined))
                        {
                            cell_paths.push_back(std::move(combined));
                        }
                    }
                }
            }

            deduplicate_paths(cell_paths);
            result[row][column] = std::move(cell_paths);
        }
    }

    return result;
}

void remove_repeated_node_paths(SymMatrix& matrix)
{
    validate_square_symbolic_matrix(matrix);

    for (auto& row : matrix)
    {
        for (auto& cell : row)
        {
            SymPaths filtered_paths;
            filtered_paths.reserve(cell.size());

            for (const auto& path : cell)
            {
                if (!path_has_repeated_nodes(path))
                {
                    filtered_paths.push_back(path);
                }
            }

            deduplicate_paths(filtered_paths);
            cell.swap(filtered_paths);
        }
    }
}

CircuitResult find_hamiltonian_circuits(const std::vector<std::vector<double>>& adj_matrix, int start_node)
{
    validate_square_numeric_matrix(adj_matrix);

    const size_t size = adj_matrix.size();
    if (size < 2)
    {
        return {false, {}};
    }

    if (start_node < -1 || start_node >= static_cast<int>(size))
    {
        throw std::invalid_argument("Start node index is out of range");
    }

    const SymMatrix base_matrix = build_symbolic_matrix(adj_matrix);
    SymMatrix current = base_matrix;

    for (size_t edge_count = 2; edge_count <= size - 1; ++edge_count)
    {
        current = symbolic_matrix_multiply(current, base_matrix);
    }

    std::unordered_set<std::string> seen_circuits;
    std::vector<SymPath> circuits;

    for (size_t start = 0; start < size; ++start)
    {
        if (start_node >= 0 && start != static_cast<size_t>(start_node))
        {
            continue;
        }

        for (size_t end = 0; end < size; ++end)
        {
            if (adj_matrix[end][start] <= 0.0)
            {
                continue;
            }

            for (const auto& path : current[start][end])
            {
                if (path.size() != size)
                {
                    continue;
                }

                SymPath circuit = path;
                circuit.push_back(static_cast<int>(start));

                const SymPath normalized = (start_node >= 0) ? circuit : canonicalize_circuit(circuit);
                if (seen_circuits.insert(path_key(normalized)).second)
                {
                    circuits.push_back(std::move(circuit));
                }
            }
        }
    }

    return {!circuits.empty(), std::move(circuits)};
}

} // namespace server