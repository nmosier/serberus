#include "clou/FordFulkerson.h"

#include <cassert>
#include <queue>
#include <stack>
#include <numeric>

#include <llvm/ADT/BitVector.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/ADT/Hashing.h>

namespace clou {

  using Graph = std::vector<std::map<unsigned, unsigned>>;

  class ScopedGraph {
  public:
    ScopedGraph(const Graph& orig): orig(orig) {}

    std::map<unsigned, unsigned> preds_weight(unsigned src) const {
      std::map<unsigned, unsigned> dsts = orig.at(src);
      const auto mod_src_it = mod.find(src);
      if (mod_src_it != mod.end())
	for (const auto& [dst, w] : mod_src_it->second)
	  dsts.insert_or_assign(dst, w);
      std::erase_if(dsts, [] (const auto& p) { return p.second == 0; }); // erase 0-weight edges
      return dsts;
    }

    unsigned get_weight(unsigned src, unsigned dst) const {
      const auto mod_src_it = mod.find(src);
      if (mod_src_it != mod.end()) {
	const auto& mod_dsts = mod_src_it->second;
	const auto mod_dst_it = mod_dsts.find(dst);
	if (mod_dst_it != mod_dsts.end())
	  return mod_dst_it->second;
      }
      const auto& orig_dsts = orig.at(src);
      const auto orig_it = orig_dsts.find(dst);
      if (orig_it != orig_dsts.end())
	return orig_it->second;
      return 0;
    }

    void put_weight(unsigned src, unsigned dst, unsigned w) {
      mod[src].insert_or_assign(dst, w);
    }

    const std::map<unsigned, unsigned> operator[](unsigned src) const {
      return preds_weight(src);
    }

  private:
    const Graph& orig;
    std::map<unsigned, std::map<unsigned, unsigned>> mod;
  };

  /* Idea: add original edges to other map. 
   *
   *
   */

  // Need ``unique'' iterator that, given a binary predicate, guarantees that if two iterators are equal,
  // returns the newer version.

  static bool find_st_path(int n, const ScopedGraph& G, int s, int t, std::vector<int>& path) {
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

  template <class graph_type>
  static llvm::BitVector find_reachable(int n, const graph_type& G, int s) {
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
  
  std::vector<std::pair<int, int>> ford_fulkerson(unsigned n, Graph& G, unsigned s, unsigned t) {
    if (n == 0)
      return {};
    
    const unsigned orig_n = n;
    if (s == t) {
      // Duplicate node so that we guarantee s != t.
      const unsigned t_new = n++;

      // All edges into s/t now go into t_new.
      for (auto& dsts : G) {
	const auto it = dsts.find(t);
	if (it != dsts.end()) {
	  const unsigned w = it->second;
	  dsts.erase(it);
	  dsts[t_new] = w;
	}
      }

      // Add edge from t_new to s.
      G.push_back({{s, std::numeric_limits<unsigned>::max()}});

      // Replace t with t_new.
      t = t_new;
    }

      
    assert(static_cast<size_t>(n) == G.size());
    assert(s != t);

    ScopedGraph ResG(G);

    std::vector<int> path;
    while (find_st_path(n, ResG, s, t, path)) {
      assert(llvm::all_of(G, [] (const std::map<unsigned, unsigned>& x) {
	return llvm::all_of(x, [] (const auto& p) {
	  return p.second >= 0;
	});
      }));
      
      unsigned path_flow = std::numeric_limits<unsigned>::max();
      assert(path.size() >= 2);
      for (auto it1 = path.begin(), it2 = std::next(it1); it2 != path.end(); ++it1, ++it2)
	path_flow = std::min(path_flow, ResG[*it1].at(*it2));
      assert(path_flow > 0);
      for (auto it1 = path.begin(), it2 = std::next(it1); it2 != path.end(); ++it1, ++it2) {
	ResG.put_weight(*it1, *it2, ResG.get_weight(*it1, *it2) - path_flow);
	ResG.put_weight(*it2, *it1, ResG.get_weight(*it2, *it1) + path_flow);
      }

      path.clear();
    }

    const llvm::BitVector reach = find_reachable(n, ResG, s);
    std::vector<std::pair<int, int>> results;
    for (unsigned u : reach.set_bits()) {
      for (const auto& [v_, w] : G[u]) {
	auto v = v_;
	assert(w > 0);
	if (!reach.test(v)) {
	  assert(G.at(u).find(v) != G.at(u).end());
	  if (orig_n != n) {
	    assert(u != t);
	    if (v == t)
	      v = s;
	  }
	  results.emplace_back(u, v);
	}
      }
    }

#ifndef NDEBUG
    if (results.empty()) {
      const llvm::BitVector reach2 = find_reachable(n, G, s);
      assert(s == t || !reach2.test(t));
    }
#endif

    // Revert to original graph
    if (orig_n != n) {
      G.pop_back();
      for (auto& dsts : G) {
	const auto it = dsts.find(t);
	if (it != dsts.end()) {
	  const auto w = it->second;
	  dsts.erase(it);
	  dsts[s] = w;
	}
      }
    }

    assert(orig_n == G.size());
#if 0
    assert(llvm::all_of(G, [orig_n] (const auto& dsts) -> bool {
      return llvm::all_of(dsts, [orig_n] (const auto& p) -> bool {
	return p.first < orig_n;
      });
    }));
#endif
    
    return results;
  }
  
}
