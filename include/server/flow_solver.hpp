#ifndef FLOW_SOLVER_HPP
#define FLOW_SOLVER_HPP

#include <vector>

namespace server
{

/**
 * @brief Performs Breadth-First Search to find an augmenting path in the residual graph.
 *
 * @param residual_graph The residual capacity matrix.
 * @param s Source node index.
 * @param t Sink node index.
 * @param parent Output array storing the path.
 * @return true if a path from s to t is found, false otherwise.
 */
bool bfs(const std::vector<std::vector<double>>& residual_graph, int s, int t, std::vector<int>& parent);

/**
 * @brief Calculates the maximum flow using the Ford-Fulkerson algorithm (Edmonds-Karp BFS variation).
 *
 * @param capacity_graph The initial capacity matrix (costs represent max flow capacity).
 * @param source Source node index.
 * @param sink Sink node index.
 * @return The maximum flow value from source to sink.
 */
double ford_fulkerson(const std::vector<std::vector<double>>& capacity_graph, int source, int sink);

} // namespace server

#endif // FLOW_SOLVER_HPP
