// C++ program for finding minimum cut using Ford-Fulkerson
#include <iostream>
#include <limits.h>
#include <queue>
#include <string.h>

#include "min-cut.h"

// Number of vertices in given graph

namespace {

/* Returns true if there is a path from source 's' to sink 't' in
residual graph. Also fills parent[] to store the path */
int bfs(std::vector<std::vector<int>> rGraph, int s, int t, int parent[]) {
  const int V = rGraph.size();

  // Create a visited array and mark all vertices as not visited
  std::vector<bool> visited(V, false);

  // Create a queue, enqueue source vertex and mark source vertex
  // as visited
  std::queue<int> q;
  q.push(s);
  visited[s] = true;
  parent[s] = -1;

  // Standard BFS Loop
  while (!q.empty()) {
    int u = q.front();
    q.pop();

    for (int v = 0; v < V; v++) {
      if (visited[v] == false && rGraph[u][v] > 0) {
        q.push(v);
        parent[v] = u;
        visited[v] = true;
      }
    }
  }

  // If we reached sink in BFS starting from source, then return
  // true, else false
  return (visited[t] == true);
}

// A DFS based function to find all reachable vertices from s. The function
// marks visited[i] as true if i is reachable from s. The initial values in
// visited[] must be false. We can also use BFS to find reachable vertices
void dfs(std::vector<std::vector<int>> rGraph, int s,
         std::vector<bool> &visited) {
  const int V = rGraph.size();
  visited[s] = true;
  for (int i = 0; i < V; i++)
    if (rGraph[s][i] && !visited[i])
      dfs(rGraph, i, visited);
}

} // namespace

// Prints the minimum s-t cut
std::vector<std::pair<int, int>> minCut(std::vector<std::vector<int>> graph,
                                        int s, int t) {
  const int V = graph.size();
  int u, v;
  std::vector<std::pair<int, int>> res;

  // Create a residual graph and fill the residual graph with
  // given capacities in the original graph as residual capacities
  // in residual graph
  std::vector<std::vector<int>> rGraph = graph;

  std::vector<int> parent(V);

  // Augment the flow while there is a path from source to sink
  while (bfs(rGraph, s, t, parent.data())) {
    // Find minimum residual capacity of the edhes along the
    // path filled by BFS. Or we can say find the maximum flow
    // through the path found.
    int path_flow = INT_MAX;
    for (v = t; v != s; v = parent[v]) {
      u = parent[v];
      path_flow = std::min(path_flow, rGraph[u][v]);
    }

    // update residual capacities of the edges and reverse edges
    // along the path
    for (v = t; v != s; v = parent[v]) {
      u = parent[v];
      rGraph[u][v] -= path_flow;
      rGraph[v][u] += path_flow;
    }
  }

  // Flow is maximum now, find vertices reachable from s
  std::vector<bool> visited(V, false);
  dfs(rGraph, s, visited);

  // Print all edges that are from a reachable vertex to
  // non-reachable vertex in the original graph
  for (int i = 0; i < V; i++)
    for (int j = 0; j < V; j++)
      if (visited[i] && !visited[j] && graph[i][j])
        res.emplace_back(i, j);

  return res;
}

#if 0
// Driver program to test above functions
int main()
{
  // Let us create a graph shown in the above example
  int graph[V][V] = { {0, 16, 13, 0, 0, 0},
            {0, 0, 10, 12, 0, 0},
            {0, 4, 0, 0, 14, 0},
            {0, 0, 9, 0, 0, 20},
            {0, 0, 0, 7, 0, 4},
            {0, 0, 0, 0, 0, 0}
          };

  minCut(graph, 0, 5);

  return 0;
}
#endif
