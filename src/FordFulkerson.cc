#include "clou/FordFulkerson.h"

#include <cassert>
#include <queue>
#include <stack>

#include <llvm/ADT/BitVector.h>
#include <llvm/Support/raw_ostream.h>

namespace clou {

  static bool find_st_path(int n, const std::vector<std::map<int, int>>& G, int s, int t, std::vector<int>& path) {
    std::vector<int> parent(n, -1);
    llvm::BitVector visited(n, false);
    std::queue<int> queue;
    queue.push(s);

    while (!queue.empty()) {
      int u = queue.front();
      queue.pop();

      for (const auto& [v, w] : G[u]) {
	if (!visited.test(v) && w > 0) {
	  queue.push(v);
	  parent[v] = u;
	  visited.set(v);
	}
      }
    }

    if (!visited.test(t))
      return false;

    // prepare path
    assert(path.empty());
    int v = t;
    do {
      assert(v >= 0);
      path.push_back(v);
      const int u = parent[v];
      assert(u >= 0);
      assert(G[u].at(v) > 0);
      v = u;
    } while (v != s);
    path.push_back(v);
    std::reverse(path.begin(), path.end());
#ifndef NDEBUG
    for (auto it1 = path.begin(), it2 = std::next(it1); it2 != path.end(); ++it1, ++it2)
      assert(G[*it1].at(*it2) > 0);
#endif
    return true;
  }

  static llvm::BitVector find_reachable(int n, const std::vector<std::map<int, int>>& G, int s) {
    llvm::BitVector reach(n, false);
    std::stack<int> stack;
    stack.push(s);
    while (!stack.empty()) {
      int u = stack.top();
      stack.pop();
      if (reach.test(u))
	continue;
      reach.set(u);
      for (const auto& [v, w] : G[u])
	if (w > 0)
	  stack.push(v);
    }
    return reach;
  }
  
  std::vector<std::pair<int, int>> ford_fulkerson(int n, const std::vector<std::map<int, int>>& G, int s, int t) {
    assert(n >= 0);
    assert(static_cast<size_t>(n) == G.size());

    if (n == 0)
      return {};

    std::vector<std::map<int, int>> ResG = G;

    std::vector<int> path;
    while (find_st_path(n, ResG, s, t, path)) {
      assert(llvm::all_of(G, [] (const std::map<int, int>& x) {
	return llvm::all_of(x, [] (const auto& p) {
	  return p.second >= 0;
	});
      }));
      
      int path_flow = INT_MAX;
      assert(path.size() >= 2);
      for (auto it1 = path.begin(), it2 = std::next(it1); it2 != path.end(); ++it1, ++it2)
	path_flow = std::min(path_flow, ResG[*it1][*it2]);
      assert(path_flow > 0);
      for (auto it1 = path.begin(), it2 = std::next(it1); it2 != path.end(); ++it1, ++it2) {
	ResG[*it1][*it2] -= path_flow;
	ResG[*it2][*it1] += path_flow;
      }

      path.clear();
    }

    const llvm::BitVector reach = find_reachable(n, ResG, s);
    std::vector<std::pair<int, int>> results;
    for (int u = 0; u < n; ++u)
      if (reach.test(u))
	for (const auto& [v, w] : ResG[u])
	  if (reach.test(v) && w > 0)
	    results.emplace_back(u, v);

    return results;
  }
  
}
