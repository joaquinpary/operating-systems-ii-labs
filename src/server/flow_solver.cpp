#include "flow_solver.hpp"
#include <algorithm>
#include <limits>
#include <queue>

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

double ford_fulkerson(const std::vector<std::vector<double>>& capacity_graph, int source, int sink)
{
    int V = capacity_graph.size();
    if (V == 0 || source < 0 || source >= V || sink < 0 || sink >= V || source == sink)
    {
        return 0.0;
    }

    std::vector<std::vector<double>> residual_graph = capacity_graph;
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

    return max_flow;
}

} // namespace server
