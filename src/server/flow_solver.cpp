#include "flow_solver.hpp"
#include <algorithm>
#include <chrono>
#include <limits>
#include <queue>

#ifdef USE_OPENMP
#include <omp.h>
#endif

#include <common/logger.h>

namespace server
{

bool bfs(const std::vector<std::vector<double>>& residual_graph, int s, int t, std::vector<int>& parent)
{
    int V = residual_graph.size();
    if (V == 0 || s < 0 || s >= V || t < 0 || t >= V)
    {
        return false;
    }

    std::vector<bool> visited(V, false);
    std::queue<int> q;

    q.push(s);
    visited[s] = true;
    parent[s] = -1;

    while (!q.empty())
    {
        int u = q.front();
        q.pop();

        for (int v = 0; v < V; v++)
        {
            if (!visited[v] && residual_graph[u][v] > 0)
            {
                if (v == t)
                {
                    parent[v] = u;
                    return true;
                }
                q.push(v);
                parent[v] = u;
                visited[v] = true;
            }
        }
    }
    return false;
}

FlowResult ford_fulkerson(const std::vector<std::vector<double>>& capacity_graph, int source, int sink)
{
    int V = capacity_graph.size();
    if (V == 0 || source < 0 || source >= V || sink < 0 || sink >= V || source == sink)
    {
        return {0.0, 0.0};
    }

#ifdef USE_OPENMP
    double t_start = omp_get_wtime();
#else
    auto t_start = std::chrono::high_resolution_clock::now();
#endif

    std::vector<std::vector<double>> residual_graph(V, std::vector<double>(V, 0.0));
#ifdef USE_OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < V; ++i)
    {
        residual_graph[i] = capacity_graph[i];
    }

    std::vector<int> parent(V);
    double max_flow = 0.0;

    while (bfs(residual_graph, source, sink, parent))
    {
        double path_flow = std::numeric_limits<double>::max();
        for (int v = sink; v != source; v = parent[v])
        {
            int u = parent[v];
            path_flow = std::min(path_flow, residual_graph[u][v]);
        }

        for (int v = sink; v != source; v = parent[v])
        {
            int u = parent[v];
            residual_graph[u][v] -= path_flow;
            residual_graph[v][u] += path_flow;
        }

        max_flow += path_flow;
    }

#ifdef USE_OPENMP
    double t_end = omp_get_wtime();
    LOG_INFO_MSG("[Profiling] Ford-Fulkerson executed in %.6f seconds (OpenMP)", t_end - t_start);
    double exec_time = (t_end - t_start) * 1000.0;
#else
    auto t_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = t_end - t_start;
    LOG_INFO_MSG("[Profiling] Ford-Fulkerson executed in %.6f seconds (Sequential)", diff.count());
    double exec_time = diff.count() * 1000.0;
#endif

    return {max_flow, exec_time};
}

} // namespace server
