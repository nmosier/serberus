#include "clou/FordFulkerson.h"

#include <cassert>
#include <queue>
#include <stack>
#include <numeric>
#include <sstream>
#include <fstream>

#include <llvm/ADT/BitVector.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/ADT/Hashing.h>
#include <llvm/Support/WithColor.h>
#include <llvm/Support/raw_os_ostream.h>

namespace clou {

  class Graph {
  public:
    using Node = unsigned;
    using Weight = unsigned;
    using Dsts = std::map<Node, Weight>;
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
    ImmutableVectorGraph(const std::vector<Dsts> *G): G(*G) {}

    using iterator = Dsts::const_iterator;
    using range_type = llvm::iterator_range<iterator>;
    range_type operator[](Node src) const {
      return range_type(G.at(src));
    }

    range_type empty_range() const {
      static Dsts dummy;
      return range_type(dummy);
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

  #if 0
  template <class BaseGraph>
  class DupGraph final : public ImmutableGraph {
  public:
    using Level = unsigned;
    
    DupGraph(const BaseGraph *orig, Level levels): G(*orig), levels(levels) {
      assert(levels >= 1);
    }

    unsigned nodes() const override {
      return G.nodes() * levels;
    }

  private:
    using base_iterator = typename BaseGraph::iterator;
    using base_range_type = typename BaseGraph::range_type;
  public:
    class iterator {
    private:
      llvm::iterator_range<base_iterator> real;
      base_iterator it;
      Level level;
      unsigned nodes;
    public:
      iterator(): real(base_iterator(), base_iterator()) {}
      iterator(const llvm::iterator_range<base_iterator>& real, base_iterator it, Level level, unsigned nodes):
	real(real), it(it), level(level), nodes(nodes) {}

      const std::pair<const Node, Weight> operator*() const {
	std::pair<Node, Weight> p = *it;
	p.first += level * nodes;
	return p;
      }

      iterator& operator++() {
	assert(it != real.end());
	++it;
	if (it == real.end()) {
	  ++level;
	  it = real.begin();
	}
	return *this;
      }

      iterator& operator++(int) {
	return this->operator++();
      }

      bool operator==(const iterator& o) const {
	assert(real.begin() == o.real.begin() && real.end() == o.real.end() && nodes == o.nodes);
	return it == o.it && level == o.level;
      }

      bool operator!=(const iterator& o) const {
	return !this->operator==(o);
      }
    };

    using range_type = llvm::iterator_range<iterator>;

    range_type operator[](Node src) const {
      assert(src / G.nodes() < levels);
      const auto real = G[src % G.nodes()];
      const iterator begin(real, real.begin(), 0, G.nodes());
      const iterator end(real, real.begin(), levels, G.nodes());
      if (real.empty())
	return range_type(end, end);
      else
	return range_type(begin, end);
    }

    Weight get_weight(Node src, Node dst) const override {
      return G.get_weight(src % G.nodes(), dst % G.nodes());
    }

    Node get_last_level_node(Node v) const {
      assert(v / G.nodes() == 0);
      return v + (levels - 1) * G.nodes();
    }

    Node get_first_level_node(Node v) const {
      return v % G.nodes();
    }

    Node get_node_for_level(Node v, Level l) const {
      assert(v < G.nodes());
      assert(l < levels);
      return v + G.nodes() * l;
    }
    
  private:
    const BaseGraph& G;
    unsigned levels;
  };
#elif 0
  template <class BaseGraph>
  class DupGraph final : public ImmutableGraph {
    static_assert(std::is_base_of_v<Graph, BaseGraph>, "Base graph must be derived from Graph class");
  public:
    DupGraph(const BaseGraph *orig): G(*orig) {}

    unsigned nodes() const override {
      return G.nodes() * 2;
    }

  private:
    using base_iterator = typename BaseGraph::iterator;
    using base_range_type = typename BaseGraph::range_type;
  public:
    class iterator {
    public:
      iterator(): real(base_iterator(), base_iterator()) {}
      iterator(const llvm::iterator_range<base_iterator>& real, base_iterator it, bool shadow, unsigned n): real(real), it(it), shadow(shadow), n(n) {}

      const std::pair<const Node, Weight> operator*() const {
	std::pair<Node, Weight> p = *it;
	if (shadow)
	  p.first += n;
	return p;
      }
      
      iterator& operator++() {
	assert(it != real.end());
	++it;
	if (it == real.end() && !shadow) {
	  shadow = true;
	  it = real.begin();
	}
	return *this;
      }

      iterator& operator++(int) {
	return operator++();
      }

      bool operator==(const iterator& o) const {
	assert(real.begin() == o.real.begin() && real.end() == o.real.end() && n == o.n);
	if (it == real.end())
	  return o.it == real.end();
	else
	  return it == o.it && shadow == o.shadow;
      }

      bool operator!=(const iterator& o) const {
	return !operator==(o);
      }
      
    private:
      llvm::iterator_range<base_iterator> real;
      base_iterator it;
      bool shadow;
      unsigned n;

      bool isEnd() const {
	return it == real.end();
      }
    };

    using range_type = llvm::iterator_range<iterator>;

    range_type operator[](Node src) const {
      const auto real = (src < G.nodes()) ? G[src] : G.empty_range();
      const iterator begin(real, real.begin(), false, G.nodes());
      const iterator end(real, real.end(), true, G.nodes());
      return range_type(begin, end);
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
    const BaseGraph& G;
  };
#else
  template <class BaseGraph>
  class DupGraph final : public ImmutableGraph {
  public:
    using Level = unsigned;
  private:
    const BaseGraph& G;
    Level levels;
  public:
    DupGraph(const BaseGraph *orig, Level levels): G(*orig), levels(levels) {
      assert(levels >= 1);
    }

    unsigned nodes() const override {
      return G.nodes() * levels;
    }
    
  private:
    using base_iterator = typename BaseGraph::iterator;
    using base_range_type = typename BaseGraph::range_type;
  public:
    class iterator {
    private:
      base_iterator it;
      unsigned offset;
    public:
      iterator() = default;
      iterator(base_iterator it, unsigned offset): it(it), offset(offset) {}

      const std::pair<const Node, Weight> operator*() const {
	std::pair<Node, Weight> p = *it;
	p.first += offset;
	return p;
      }

      iterator& operator++() {
	++it;
	return *this;
      }

      iterator& operator++(int) {
	return this->operator++();
      }

      bool operator==(const iterator& o) const {
	assert(offset == o.offset);
	return it == o.it;
      }

      bool operator!=(const iterator& o) const {
	return !operator==(o);
      }

      using iterator_category = typename base_iterator::iterator_category;
    };

    using range_type = llvm::iterator_range<iterator>;
    
  private:
    Level get_level(Node v) const {
      return v / G.nodes();
    }

    Node get_base(Node v) const {
      return v % G.nodes();
    }
  public:
    range_type operator[](Node src) const {
      const auto range = G[get_base(src)];
      const unsigned offset = get_level(src) * G.nodes();
      const iterator begin(range.begin(), offset);
      const iterator end(range.end(), offset);
      return range_type(begin, end);
    }

    Weight get_weight(Node src, Node dst) const override {
      if (get_level(src) == get_level(dst))
	return G.get_weight(get_base(src), get_base(dst));
      else
	return 0;
    }

    Node get_first_level_node(Node v) const {
      return v % G.nodes();
    }

    Node get_node_for_level(Node v, Level l) const {
      assert(v < G.nodes());
      return v + G.nodes() * l;
    }

    Node inc_level(Node v) const {
      return get_node_for_level(v, get_level(v) + 1);
    }

    Node dec_level(Node v) const {
      return get_node_for_level(v, get_level(v) - 1);
    }
  };
  
#endif

  template <class BaseGraph>
  class ScopedGraph final : public MutableGraph {
    static_assert(std::is_base_of_v<Graph, BaseGraph>, "Base graph must be derived from Graph class");
  public:
    ScopedGraph(const BaseGraph *orig): orig(*orig) {}

    unsigned nodes() const override {
      return orig.nodes();
    }

    class iterator {
    public:
      using orig_iterator = typename BaseGraph::iterator;
      using mod_iterator = Dsts::const_iterator;
      iterator() {}
      iterator(orig_iterator it): orig(true), orig_it(it) {}
      iterator(mod_iterator it): orig(false), mod_it(it) {}

      std::pair<const Node, Weight> operator*() const {
	if (orig)
	  return *orig_it;
	else
	  return *mod_it;
      }

      iterator& operator++() {
	if (orig)
	  ++orig_it;
	else
	  ++mod_it;
	return *this;
      }

      bool operator==(const iterator& o) const {
	assert(orig == o.orig);
	if (orig)
	  return orig_it == o.orig_it;
	else
	  return mod_it == o.mod_it;
      }

      bool operator!=(const iterator& o) const {
	return !operator==(o);
      }

    private:
      bool orig;
      orig_iterator orig_it;
      mod_iterator mod_it;
    };

    using range_type = llvm::iterator_range<iterator>;

    range_type operator[](Node src) const {
      const auto it = mod.find(src);
      iterator begin, end;
      if (it != mod.end()) {
	begin = iterator(it->second.begin());
	end = iterator(it->second.end());
      } else {
	const auto orig_range = orig[src];
	begin = iterator(orig_range.begin());
	end = iterator(orig_range.end());
      }
      return range_type(begin, end);
    }

    Weight get_weight(Node src, Node dst) const override {
      const auto mod_src_it = mod.find(src);
      if (mod_src_it != mod.end()) {
	const auto& mod_dsts = mod_src_it->second;
	const auto mod_dst_it = mod_dsts.find(dst);
	if (mod_dst_it != mod_dsts.end())
	  return mod_dst_it->second;
	else
	  return 0;
      } else {
	return orig.get_weight(src, dst);
      }
    }

    void put_weight(Node src, Node dst, Weight w) override {
      // NOTE: It's ok if weight is 0. This means removing an edge.
      auto it = mod.find(src);
      if (it == mod.end()) {
	it = mod.emplace(src, std::map<Node, Weight>()).first;
#if 0
	llvm::copy(orig[src], std::inserter(it->second, it->second.end()));
#else
	for (const auto& p : orig[src])
	  it->second.insert(p);
#endif
      }
      if (w > 0)
	it->second[dst] = w;
      else
	it->second.erase(dst);
    }
    
  private:
    const BaseGraph& orig;
    std::map<Node, Dsts> mod;
  };

  template <class Graph1, class Graph2>
  class IntersectUnitGraph final : public ImmutableGraph {
  public:
    IntersectUnitGraph(const Graph1 *G1, const Graph2 *G2): G1(*G1), G2(*G2) {
      assert(this->G1.nodes() == this->G2.nodes());
    }

    unsigned nodes() const override {
      return G1.nodes();
    }

    const std::map<Node, Weight> operator[](Node src) const {
      std::map<Node, Weight> dsts1, dsts2;
      llvm::transform(G1[src], std::inserter(dsts1, dsts1.end()), [] (const auto& p) {
	return std::make_pair(p.first, 1);
      });
      llvm::transform(G2[src], std::inserter(dsts2, dsts2.end()), [] (const auto& p) {
	return std::make_pair(p.first, 1);
      });
      std::map<Node, Weight> dsts;
      std::set_intersection(dsts1.begin(), dsts1.end(), dsts2.begin(), dsts2.end(), std::inserter(dsts, dsts.end()));
      return dsts;
    }

    Weight get_weight(Node src, Node dst) const override { std::abort(); }

  private:
    const Graph1& G1;
    const Graph2& G2;
  };

  template <class BaseGraph>
  static bool find_st_path(int n, const BaseGraph& G, int s, int t, std::vector<int>& path) {
    static_assert(std::is_base_of_v<Graph, BaseGraph>, "");
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

  template <class BaseGraph>
  static bool find_st_path_multi(const BaseGraph& G, llvm::ArrayRef<std::set<unsigned>> waypoint_sets,
				 std::vector<unsigned>& path) {
    static_assert(std::is_base_of_v<Graph, BaseGraph>, "");
    const auto n = G.nodes();
    assert(waypoint_sets.size() >= 2);
    assert(path.empty());

    std::vector<std::vector<int>> parents;

    std::set<unsigned> S = waypoint_sets.front();
    std::vector<std::set<unsigned>> waypoints_reached;
    for (int i = 0; const std::set<unsigned>& T : waypoint_sets.drop_front()) {
      waypoints_reached.push_back(S);
      
      llvm::BitVector visited(n, false);
      std::vector<int>& parent = parents.emplace_back(n, -1);
      std::stack<unsigned> queue;
      for (auto s : S)
	queue.push(s);

      // Find all the nodes reachable from each s \in S. 
      while (!queue.empty()) {
	const unsigned u = queue.top();
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
    waypoints_reached.push_back(S);

    if (S.empty()) {
      // No multi-s-t path was found.
      return false;
    }
    
    // Find multi-s-t path.
    {
      unsigned t = *waypoints_reached.back().begin();
      for (const auto& [parent, waypoint_set] : llvm::reverse(llvm::zip(parents, llvm::ArrayRef(waypoints_reached).drop_back()))) {
	int v = static_cast<int>(t);
	while (true) {
	  path.push_back(v);
	  v = parent[v];
	  assert(v >= 0);
	  if (waypoint_set.contains(v))
	    break;
	}
	t = v;
      }
      path.push_back(t);
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

  template <class GraphT>
  static llvm::BitVector find_reachable(const GraphT& G, unsigned s) {
    llvm::BitVector reach(G.nodes(), false);
    std::stack<unsigned> todo;
    todo.push(s);
    while (!todo.empty()) {
      const unsigned u = todo.top();
      todo.pop();
      if (reach.test(u))
	continue;
      reach.set(u);
      for (const auto& [v, w] : G[u])
	todo.push(v);
    }
    return reach;
  }

  template <class BaseGraph>
  static llvm::BitVector find_reachable_multi(const BaseGraph& G, const std::set<Graph::Node>& S) {
    static_assert(std::is_base_of_v<Graph, BaseGraph>, "");
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

  template <class GraphT>
  static void printGraph(const std::string& filename, const GraphT& G, llvm::ArrayRef<std::set<unsigned>> waypoint_sets,
			 const std::set<std::pair<Node, Node>>& cut = {}) {
    std::ofstream f(filename);
    llvm::raw_os_ostream os(f);
    os << "digraph {\n";

    const std::string colors[] = {"green", "yellow", "red"};

    // Define nodes.
    for (unsigned u = 0; u < G.nodes(); ++u) {
      os << "node" << u << " [label=\"" << u << "\"";
      const auto waypoint_it = llvm::find_if(waypoint_sets, [&] (const auto& waypoint_set) {
	return waypoint_set.contains(u);
      });
      if (waypoint_it != waypoint_sets.end()) {
	os << ", fillcolor=" << colors[waypoint_it - waypoint_sets.begin()] << ", style=filled, fontcolor=black";
      }
      os << "];\n";
    }

    // Define edges.
    for (unsigned u = 0; u < G.nodes(); ++u) {
      for (const auto& [v, w] : G[u]) {
	os << "node" << u << " -> node" << v << " [label=\"" << w << "\"";
	if (cut.contains({u, v}))
	  os << ", color=red";
	os << "];\n";
      }
    }

    os << "}\n";
  }

  template <class GraphT>
  static void ford_fulkerson_multi_impl(const GraphT& G, llvm::ArrayRef<std::set<unsigned>> waypoint_sets,
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
	path_flow = std::min(path_flow, ResG.get_weight(*it1, *it2));
      assert(path_flow > 0 && path_flow < std::numeric_limits<unsigned>::max());
      for (auto it1 = path.begin(), it2 = std::next(it1); it2 != path.end(); ++it1, ++it2) {
	ResG.put_weight(*it1, *it2, ResG.get_weight(*it1, *it2) - path_flow);
	ResG.put_weight(*it2, *it1, ResG.get_weight(*it2, *it1) + path_flow);
      }
      path.clear();
    }

#if 0
    printGraph("residual.dot", ResG, waypoint_sets);
#endif

    // NOTE: This is suspicious. I think we need to rewrite this for the multi-s-t case.
#if 0
    {
      const auto& S = waypoint_sets.front();
      const IntersectUnitGraph IntersectG(&G, &ResG);
      const llvm::BitVector reach = find_reachable_multi(IntersectG, S);
      for (const unsigned u : reach.set_bits()) {
	for (const auto& [v, _] : G[u]) {
	  if (!reach.test(v))
	    results.emplace_back(u, v);
	}
      }
    }
#elif 0
    {
      for (const auto& S : waypoint_sets.drop_back()) {
	for (const Node s : S) {
	  // TODO: Do all these S's together?
	  const llvm::BitVector reach = find_reachable(ResG, s);
	  for (const Node u : reach.set_bits())
	    for (const auto& [v, w] : G[u])
	      if (!reach.test(v))
		results.emplace_back(u, v);
	}
      }
    }
#else
    {
      for (const auto& S : waypoint_sets.drop_back()) {
	const llvm::BitVector reach = find_reachable_multi(ResG, S);
	for (const Node u : reach.set_bits())
	  for (const auto& [v, w] : G[u])
	    if (!reach.test(v))
	      results.emplace_back(u, v);
      }
    }
#endif

#if 0
    {
      const IntersectUnitGraph IntersectG(&G, &ResG);
      printGraph("intersect.dot", IntersectG, waypoint_sets);
    }
#endif
  }

  std::vector<std::pair<unsigned, unsigned>>
  ford_fulkerson_multi(const std::vector<std::map<unsigned, unsigned>>& G, llvm::ArrayRef<std::set<unsigned>> waypoint_sets) {
    const ImmutableVectorGraph OrigG(&G);
    const DupGraph DupG(&OrigG, waypoint_sets.size());
    ScopedGraph ModG(&DupG); // For manually adding in connections from different levels.
    
    // Need to replace last transmitters with their shadow nodes.
    std::vector<std::set<unsigned>> waypoints_mod(waypoint_sets.size());
    for (unsigned i = 0; const auto& [in, out] : llvm::zip(waypoint_sets, waypoints_mod)) {
      llvm::transform(in, std::inserter(out, out.end()), [&] (Node u) -> Node {
	return DupG.get_node_for_level(u, i);
      });

      if (i > 0)
	for (Node u = 0; u < OrigG.nodes(); ++u) {
	  for (const auto& [v, w] : OrigG[u]) {
	    if (in.contains(v)) {
	      // Add level-up u->v edge.
	      ModG.put_weight(DupG.get_node_for_level(u, i - 1),
			      DupG.get_node_for_level(v, i),
			      w);

	      // Delete same-level u->v edge.
	      ModG.put_weight(DupG.get_node_for_level(u, i - 1),
			      DupG.get_node_for_level(v, i - 1),
			      0);
	    }
	  }

	}
      
      ++i;
    }

    // printGraph("graph.dot", DupG, waypoints_mod);

    std::vector<std::pair<Node, Node>> results;
    ford_fulkerson_multi_impl(ModG, waypoints_mod, results);

    // Convert results back to real.
    for (auto& e : results) {
      e.first = DupG.get_first_level_node(e.first);
      e.second = DupG.get_first_level_node(e.second);
    }

    llvm::sort(results);
#if 0
#ifndef NDEBUG
    if (results.size() >= 2) {
      llvm::ArrayRef<std::pair<Node, Node>> results_ref(results);
      for (const auto& [p1, p2] : llvm::zip(results_ref.drop_back(), results_ref.drop_front()))
	if (p1 == p2)
	  llvm::WithColor::warning() << "duplicate cut edges\n";
    }
#endif
#endif
    {
      const auto new_results_end = std::unique(results.begin(), results.end());
      results.resize(new_results_end - results.begin());
    }
    
    return results;
  }
  
}
