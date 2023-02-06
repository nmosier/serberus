#include "clou/FordFulkerson.h"

#include <cassert>
#include <queue>
#include <stack>
#include <numeric>

#include <llvm/ADT/BitVector.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/ADT/Hashing.h>

namespace clou {

  class Graph {
  public:
    using Node = unsigned;
    using Weight = unsigned;
    using Dsts = std::map<Node, Weight>;
    virtual const Dsts operator[](Node src) const = 0;
    virtual Weight get_weight(Node src, Node dst) const = 0; // Returns 0 if no edge exists.
    virtual unsigned nodes() const = 0;
  };

  class ImmutableGraph : public Graph {};

  class MutableGraph : public Graph {
  public:
    virtual void put_weight(Node src, Node dst, Weight w) = 0;
  };

  class ImmutableVectorGraph final : public ImmutableGraph {
  public:
    ImmutableVectorGraph(const std::vector<Dsts> *G): G(*G) {
      // Make sure we don't have any zero weights to start.
      assert(llvm::all_of(this->G, [] (const std::map<Node, Node>& dsts) {
	return llvm::all_of(dsts, [] (const auto& p) {
	  return p.second > 0;
	});
      }));
    }

    const Dsts operator[](Node src) const override {
      assert(llvm::all_of(G.at(src), [] (const auto& p) { return p.second > 0; }));
      return G.at(src);
    }

    Weight get_weight(Node src, Node dst) const override {
      const auto& dsts = G.at(src);
      const auto it = dsts.find(dst);
      if (it == dsts.end())
	return 0;
      else
	return it->second;
    }

    unsigned nodes() const override {
      return G.size();
    }
    
  private:
    const std::vector<Dsts>& G;
  };

  class DupGraph final : public ImmutableGraph {
  public:
    DupGraph(const Graph *orig): G(*orig) {}

    unsigned nodes() const override {
      return G.nodes() * 2;
    }

    const Dsts operator[](Node src) const override {
      // Shadow nodes never have any successors.
      if (src >= G.nodes())
	return {};
      
      Dsts dsts;
      for (const auto& [dst, w] : G[src]) {
	assert(w > 0);
	dsts[dst] = w;
	dsts[dst + G.nodes()] = w;
      }
      return dsts;
    }

    Weight get_weight(Node src, Node dst) const override {
      if (src >= G.nodes())
	return 0;
      if (dst >= G.nodes())
	dst -= G.nodes();
      return G.get_weight(src, dst);
    }

    Node get_shadow_node(Node v) const {
      assert(v < G.nodes());
      return v + G.nodes();
    }

    Node get_real_node(Node v) const {
      assert(v >= G.nodes());
      return v - G.nodes();
    }
    
  private:
    const Graph& G;
  };

  class ScopedGraph final : public MutableGraph {
  public:
    ScopedGraph(const Graph *orig): orig(*orig) {}

    unsigned nodes() const override {
      return orig.nodes();
    }

    const Dsts operator[](Node src) const override {
      Dsts dsts = orig[src];
      const auto mod_it = mod.find(src);
      if (mod_it != mod.end())
	for (const auto& [dst, w] : mod_it->second)
	  dsts.insert_or_assign(dst, w);
      std::erase_if(dsts, [] (const auto& p) { return p.second == 0; });
      return dsts;
    }

    Weight get_weight(Node src, Node dst) const override {
      const auto mod_src_it = mod.find(src);
      if (mod_src_it != mod.end()) {
	const auto& mod_dsts = mod_src_it->second;
	const auto mod_dst_it = mod_dsts.find(dst);
	if (mod_dst_it != mod_dsts.end())
	  return mod_dst_it->second;
      }
      return orig.get_weight(src, dst);
    }

    void put_weight(Node src, Node dst, Weight w) override {
      // NOTE: It's ok if weight is 0. This means removing an edge.
      mod[src][dst] = w;
    }
    
  private:
    const Graph& orig;
    std::map<Node, Dsts> mod;
  };

  static bool find_st_path(int n, const Graph& G, int s, int t, std::vector<int>& path) {
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

  static bool find_st_path_multi(const Graph& G, llvm::ArrayRef<std::set<unsigned>> waypoint_sets,
				 std::vector<unsigned>& path) {
    const auto n = G.nodes();
    assert(waypoint_sets.size() >= 2);
    assert(path.empty());

    std::vector<std::vector<int>> parents;

    std::set<unsigned> waypoints_reached = waypoint_sets.front();
    for (const std::set<unsigned>& T : waypoint_sets.drop_front()) {
      auto& S = waypoints_reached; // Alias for waypoints_reached. When for-loop terminates, the vertices in
      // `waypoints_reached` are definitely not S, but rather T.
      
      std::vector<int>& parent = parents.emplace_back(n, -1);
      llvm::BitVector visited(n, false);
      std::queue<unsigned> queue;
      for (auto s : S)
	queue.push(s);

      // NOTE: It looks like this loop correctly handles the case when s = t.
      // I should rewrite the algorithm to handle this edge case natively without having to
      // rewrite the graph, since I'm not sure if that trick will work so easily for 3+ waypoint s-t paths.
      while (!queue.empty()) {
	const unsigned u = queue.front();
	queue.pop();
	for (const auto& [v, w] : G[u]) {
	  if (visited.test(v))
	    continue;
	  queue.push(v);
	  parent[v] = u;
	  visited.set(v);
	}
      }

      // Copy over the vertices that were reached. 
      S.clear();
      llvm::copy_if(T, std::inserter(S, S.end()), [&visited] (unsigned t) -> bool { return visited.test(t); });
      
    }

    if (waypoints_reached.empty())
      // No multi-s-t path was found.
      return false;
    
    // Find multi-s-t path.
    const unsigned terminal_t = *waypoints_reached.begin();
    path.push_back(terminal_t);

    for (unsigned t = terminal_t;
	 const auto& [parent, waypoint_set] :
	   llvm::reverse(llvm::zip(parents, llvm::ArrayRef(waypoint_sets).drop_back()))) {
      assert(path.back() == t);
      int v = static_cast<int>(t);
      while (true) {
	v = parent[v];
	assert(v >= 0);
	path.push_back(v);
	if (waypoint_set.contains(v))
	  break;
      }
      t = v;
      assert(path.back() == t);
    }

    std::reverse(path.begin(), path.end());
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

  static llvm::BitVector find_reachable_multi(const Graph& G, const std::set<Graph::Node>& S) {
    const auto n = G.nodes();
    llvm::BitVector reach(n, false);
    std::stack<unsigned> stack;
    for (unsigned s : S)
      stack.push(s);
    while (!stack.empty()) {
      unsigned u = stack.top();
      stack.pop();
      if (reach.test(u))
	continue;
      reach.set(u);
      for (const auto& [v, w] : G[u]) {
	assert(w > 0);
	stack.push(v);
      }
    }
    return reach;
  }

#if 0
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
#endif

#if 0
  std::vector<std::pair<unsigned, unsigned>>
  ford_fulkerson_multi(unsigned n, Graph& G, const std::set<unsigned>& S_, const std::set<unsigned>& T_) {
    auto S = S_;
    auto T = T_;
    
    if (n == 0)
      return {};

    std::map<unsigned, unsigned> dups;
    {
      std::set<unsigned> dups_tmp;
      std::set_intersection(S.begin(), S.end(), T.begin(), T.end(), std::inserter(dups_tmp, dups_tmp.end()));
      for (unsigned dup : dups_tmp) {
	const unsigned t_new = n++;
	[[maybe_unused]] const auto res = dups.emplace(t_new, dup);
	assert(res.second);

	// All edges into `dup` now go into `t_new`.
	for (auto& dsts : G) {
	  const auto it = dsts.find(dup);
	  if (it != dsts.end()) {
	    const unsigned w = it->second;
	    dsts.erase(it);
	    dsts[t_new] = w;
	  }
	}

	// Add edge from `t_new` to `dup`.
	G.push_back({{dup, std::numeric_limits<unsigned>::max()}});

	// Replace `dup` in `T` with `t_new`.
	T.erase(dup);
	T.insert(t_new);
      }
    }

    assert(n == G.size());

    ScopedGraph ResG(G);

    std::vector<unsigned> path;
    while (find_st_path_multi(n, ResG, S, T, path)) {
      unsigned path_flow = std::numeric_limits<unsigned>::max();
      assert(path.size() >= 2);
      for (auto it1 = path.begin(), it2 = std::next(it1); it2 != path.end(); ++it1, ++it2)
	path_flow = std::min(path_flow, ResG[*it1].at(*it2));
      assert(path_flow > 0 && path_flow < std::numeric_limits<unsigned>::max());
      for (auto it1 = path.begin(), it2 = std::next(it1); it2 != path.end(); ++it1, ++it2) {
	ResG.put_weight(*it1, *it2, ResG.get_weight(*it1, *it2) - path_flow);
	ResG.put_weight(*it2, *it1, ResG.get_weight(*it2, *it1) + path_flow);
      }
      path.clear();
    }

    const llvm::BitVector reach = find_reachable_multi(n, ResG, S);
    std::vector<std::pair<unsigned, unsigned>> results;
    for (unsigned u : reach.set_bits()) {
      assert(!T.contains(u));
      for (const auto& [v_, w] : G[u]) {
	auto v = v_;
	assert(w > 0);
	if (!reach.test(v)) {
	  assert(G.at(u).find(v) != G.at(u).end());
	  const auto dup_it = dups.find(v);
	  if (dup_it != dups.end())
	    v = dup_it->second;
	  results.emplace_back(u, v);
	}
      }
    }

    // Revert to original graph
    if (!dups.empty()) {
      G.resize(G.size() - dups.size());
      for (auto& dsts : G) {
	for (const auto& [t_new, t_orig] : dups) {
	  const auto it = dsts.find(t_new);
	  if (it != dsts.end()) {
	    const unsigned w = it->second;
	    dsts.erase(it);
	    dsts[t_orig] = w;
	  }
	}
      }
    }

    return results;
  }
#endif

  using Node = Graph::Node;
  using Weight = Graph::Weight;
  
  static void ford_fulkerson_multi_impl(const Graph& G, llvm::ArrayRef<std::set<unsigned>> waypoint_sets,
					std::vector<std::pair<Node, Node>>& results) {
    constexpr unsigned MAX_WEIGHT = std::numeric_limits<unsigned>::max();
    assert(waypoint_sets.size() >= 2);
    if (G.nodes() == 0)
      return;

    ScopedGraph ResG(&G);

    std::vector<unsigned> path;
    while (find_st_path_multi(ResG, waypoint_sets, path)) {
      unsigned path_flow = MAX_WEIGHT;
      assert(path.size() >= 2);
      for (auto it1 = path.begin(), it2 = std::next(it1); it2 != path.end(); ++it1, ++it2)
	path_flow = std::min(path_flow, ResG[*it1].at(*it2));
      assert(path_flow > 0 && path_flow < std::numeric_limits<unsigned>::max());
      for (auto it1 = path.begin(), it2 = std::next(it1); it2 != path.end(); ++it1, ++it2) {
	ResG.put_weight(*it1, *it2, ResG.get_weight(*it1, *it2) - path_flow);
	ResG.put_weight(*it2, *it1, ResG.get_weight(*it2, *it1) + path_flow);
      }
      path.clear();
    }

    {
      const auto& S = waypoint_sets.front();
      const auto& T = waypoint_sets.back();
      const llvm::BitVector reach = find_reachable_multi(ResG, S);
      for (const unsigned u : reach.set_bits()) {
	// FIXME: Is this assertion useful?
	assert(!T.contains(u));
	for (const auto& [v, w] : G[u]) {
	  if (!reach.test(v))
	    results.emplace_back(u, v);
	}
      }
    }
  }

  std::vector<std::pair<unsigned, unsigned>>
  ford_fulkerson_multi(const std::vector<std::map<unsigned, unsigned>>& G, llvm::ArrayRef<std::set<unsigned>> waypoint_sets) {
    const ImmutableVectorGraph OrigG(&G);
    const DupGraph DupG(&OrigG);

    // Need to replace last transmitters with their shadow nodes.
    std::vector<std::set<unsigned>> waypoints_mod;
    llvm::copy(waypoint_sets, std::back_inserter(waypoints_mod));
    auto& T = waypoints_mod.back();
    const auto OrigT = std::move(T);
    for (Node t : OrigT)
      T.insert(DupG.get_shadow_node(t));
    std::vector<std::pair<Node, Node>> results;
    ford_fulkerson_multi_impl(DupG, waypoints_mod, results);

    // Convert results back to real.
    for (auto& e : results) {
      assert(!T.contains(e.first));
      if (T.contains(e.second))
	e.second = DupG.get_real_node(e.second);
    }

    return results;
  }
  
}
