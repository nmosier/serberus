#pragma once

#include "MinCutBase.h"

#include <queue>
#include <stack>

#include <llvm/ADT/SmallSet.h>

namespace clou {

  template <class Node, class Weight>
  class MinCutGreedy final : public MinCutBase<Node, Weight> {
  public:
    using Super = MinCutBase<Node, Weight>;
    using Edge = typename Super::Edge;
    using ST = typename Super::ST;

    void run() override {
      // sort and de-duplicate sts
      {
	const std::set<ST> st_set(this->sts.begin(), this->sts.end());
	this->sts.resize(st_set.size());
	llvm::copy(st_set, this->sts.begin());
      }

#if 0
      if (this->sts.size() > 50000) {
	this->fallback = true;
	for (const ST& st : this->sts) {
	  for (const auto& [dst, _] : this->G[st.s]) {
	    const Edge e = {.src = st.s, .dst = dst};
	    this->cut_edges.push_back(e);
	  }
	}
	return;
      }
#endif

      llvm::errs() << "min-cut on " << getNodes().size() << " nodes\n";
      
      unsigned i = 0;
      while (true) {
	++i;
	llvm::errs() << "\titeration " << i << "\n";

	auto reaching_sts = computeReaching();
      
	float maxw = 0;
	Edge maxe;
	llvm::BitVector maxbv;
#if 1
	for (const auto& [e, cost] : getEdges()) {
	  const unsigned paths = reaching_sts[e].size();
	  const float w = paths * (1.f / static_cast<float>(cost));
	  if (w > maxw) {
	    maxw = w;
	    maxe = e;
	  }
	}
#else
	for (const auto& [e, sts] : reaching_sts) {
	  const unsigned paths = sts.count();
	  const auto cost = this->G.at(e.src).at(e.dst);
	  const float w = paths * (1.f / static_cast<float>(cost));
	  if (w > maxw) {
	    maxw = w;
	    maxe = e;
	    maxbv = sts;
	  }
	}
#endif

	// if no weights were > 0, we're done
	if (maxw == 0)
	  break;

	// cut the max edge and continue
	cutEdge(maxe, maxbv);
      }
    }
    
  private:
    using EdgeSet = std::set<Edge>;

    static llvm::BitVector bvand(const llvm::BitVector& a, const llvm::BitVector& b) {
      llvm::BitVector res = a;
      res &= b;
      return res;
    }

    static llvm::BitVector bvnot(const llvm::BitVector& a) {
      llvm::BitVector res = a;
      res.flip();
      return res;
    }

    void cutEdge(const Edge& e, const llvm::BitVector& maxbv) {
      this->cut_edges.push_back(e);
      this->G[e.src].erase(e.dst);
      this->G[e.dst].erase(e.src);

#if 0
      for (auto& [node, io] : fwd) {
	io.in.reset(maxbv);
	io.out.reset(maxbv);
      }
      for (auto& [node, io] : bwd) {
	io.in.reset(maxbv);
	io.out.reset(maxbv);
      }
#endif
    }

    std::set<std::pair<Edge, Weight>> getEdges() const {
      std::set<std::pair<Edge, Weight>> es;
      for (const auto& [src, dsts] : this->G)
	for (const auto& [dst, w] : dsts) {
	  const Edge e = {.src = src, .dst = dst};
	  es.emplace(e, w);
	}
      return es;
    }

    std::set<Node> bfs(const Node& root, const Node& target, std::map<Node, llvm::SmallSet<Node, 4>>& G) const {
      std::stack<Node> todo;
      std::set<Node> seen;
      todo.push(root);
      while (!todo.empty()) {
	const Node node = todo.top();
	todo.pop();
	if (seen.insert(node).second && (root == target || node != target)) {
	  // add successors
	  for (const Node& dst : G[node])
	    todo.push(dst);
	}
      }
      return seen;
    }

    static bool existsPathAvoiding(const Node& src, const Node& dst, const Node& avoid, std::map<Node, std::set<Node>>& G) {
      std::stack<Node> todo;
      todo.push(src);
      std::set<Node> seen;
      while (!todo.empty()) {
	const Node node = todo.top();
	todo.pop();
	if (!seen.insert(node).second)
	  continue;
	if (node == avoid)
	  continue;
	if (node == dst)
	  return true;
	for (const Node& succ : G[node])
	  todo.push(succ);
      }
      return false;
    }

    static unsigned countBasicBlocks(std::map<Node, std::set<Node>>& G, std::map<Node, std::set<Node>>& Grev) {
      /* The start of a basic block is a node that has no predecessors or exactly one predecessor,
       * which must have more than one successor.
       *
       */
      std::set<Node> entries;
      for (const auto& [dst, srcs] : Grev) {
	if (srcs.empty()) {
	  entries.insert(dst);
	} else if (srcs.size() == 1) {
	  if (G[*srcs.begin()].size() > 1)
	    entries.insert(dst);
	} else {
	  entries.insert(dst);
	}
      }

      return entries.size();
    }
    
#if 0
    mutable std::set<ST> disconnected_sts;
    std::map<Edge, std::set<ST>> computeReaching() {
      auto& sts = this->sts;

      std::map<Edge, std::set<ST>> results;

      std::map<Node, llvm::SmallSet<Node, 4>> G, Grev;
      for (const auto& [src, dsts] : this->G) {
	for (const auto& [dst, _] : dsts) {
	  G[src].insert(dst);
	  Grev[dst].insert(src);
	}
      }

      if (G.empty())
	return {};

      /* Approach:
       * BFS for each s-t pair.
       */

      // We're recomputing a lot of information that we don't need to recompute.
      // What if for each unique source, we compute the set of nodes it reaches.
      // And do the same of each unique sink.
      // Then for each ST, we take the intersection 

      for (int i = 0; const ST& st : sts) {
	llvm::errs() << "\r\t\tsts processed: " << i++ << "/" << sts.size();
	if (disconnected_sts.contains(st))
	  continue;
	std::set<Node> fwd, bwd;
	fwd = bfs(st.s, st.t, G);
	if (fwd.contains(st.t))
	  bwd = bfs(st.t, st.s, Grev);
	std::set<Node> both;
	std::set_intersection(fwd.begin(), fwd.end(), bwd.begin(), bwd.end(), std::inserter(both, both.end()));
	if (both.size() < 2)
	  disconnected_sts.insert(st);
	for (const Node& src : both) {
	  for (const Node& dst : G[src]) {
	    if (both.contains(dst)) {
	      const Edge edge = {.src = src, .dst = dst};
	      results[edge].insert(st);
	    }
	  }
	}
      }

      llvm::errs() << "\n";

      return results;
    }
#elif 1
    mutable std::set<ST> disconnected_sts;
    std::map<Edge, std::set<ST>> computeReaching() {
      auto& sts = this->sts;

      std::map<Edge, std::set<ST>> results;

      std::map<Node, llvm::SmallSet<Node, 4>> G, Grev;
      for (const auto& [src, dsts] : this->G) {
	for (const auto& [dst, _] : dsts) {
	  G[src].insert(dst);
	  Grev[dst].insert(src);
	}
      }

      if (G.empty())
	return {};

      /* Approach:
       * BFS for each s-t pair.
       */

      // We're recomputing a lot of information that we don't need to recompute.
      // What if for each unique source, we compute the set of nodes it reaches.
      // And do the same of each unique sink.
      // Then for each ST, we take the intersection

      std::set<Node> ss, ts;
      for (const ST& st : sts) {
	ss.insert(st.s);
	ts.insert(st.t);
      }
	
      std::map<Node, std::set<Node>> s_reach, t_reach;
      for (const Node& s : ss) {
	auto reach = bfs(s, s, G);
	s_reach.emplace(s, std::move(reach));
      }

      for (const Node& t : ts) {
	auto reach = bfs(t, t, Grev);
	t_reach.emplace(t, std::move(reach));
      }

      for (int i = 0; const ST& st : sts) {
	llvm::errs() << "\r\t\tsts processed: " << i++ << "/" << sts.size();
	if (disconnected_sts.contains(st))
	  continue;
	const auto& fwd = s_reach.at(st.s);
	const auto& bwd = t_reach.at(st.t);
	std::set<Node> both;
	std::set_intersection(fwd.begin(), fwd.end(), bwd.begin(), bwd.end(), std::inserter(both, both.end()));
	if (both.size() < 2)
	  disconnected_sts.insert(st);
	for (const Node& src : both) {
	  for (const Node& dst : G[src]) {
	    if (both.contains(dst)) {
	      const Edge edge = {.src = src, .dst = dst};
	      results[edge].insert(st);
	    }
	  }
	}
      }

      llvm::errs() << "\n";

      return results;
    }
#elif 1
    std::map<Edge, std::set<ST>> computeReaching() {
      std::set<ST> sts;
      llvm::copy(this->sts, std::inserter(sts, sts.end()));
      std::map<Edge, std::set<ST>> results;
      std::map<Node, llvm::SmallSet<Node, 2>> G, Grev;
      for (const auto& [src, dsts] : this->G) {
	for (const auto& [dst, _] : dsts) {
	  G[src].insert(dst);
	  Grev[dst].insert(src);
	}
      }

      if (G.empty())
	return {};

      std::set<Node> fwd_filter, bwd_filter;
      for (const ST& st : sts) {
	fwd_filter.insert(st.s);
	bwd_filter.insert(st.t);
      }

      std::map<Node, std::set<Node>> fwd, bwd, fwd_bak, bwd_bak;
      const auto fwd_order = topological_order(G);
      const auto bwd_order = topological_order(Grev);
      int i = 0;
      do {
	fwd_bak = fwd;
	bwd_bak = bwd;

	llvm::errs() << "\r\t\tdata-flow iteration " << ++i;

	for (const Node& dst : fwd_order) {
	  auto& v = fwd[dst];
	  // meet
	  for (const Node& src : G[dst])
	    llvm::copy(fwd[src], std::inserter(v, v.end()));
	  // transfer
	  if (fwd_filter.contains(dst))
	    v.insert(dst);
	}

	for (const Node& dst : bwd_order) {
	  auto& v = bwd[dst];
	  // meet
	  for (const Node& src : Grev[dst])
	    llvm::copy(bwd[src], std::inserter(v, v.end()));
	  // transfer
	  if (bwd_filter.contains(dst))
	    v.insert(dst);
	}
	
      } while (fwd != fwd_bak || bwd != bwd_bak);
      llvm::errs() << "\n";

      std::map<Node, std::set<Node>> stmap;
      for (const ST& st : sts)
	stmap[st.s].insert(st.t);

      // now find edges for which there is a source in the src and sink in the dst
      int j = 0;
      for (const auto& [src, dsts] : G) {
	const auto& ss = fwd[src];
	for (const Node& dst : dsts) {
	  const auto& ts = bwd[dst];
	  for (const ST& st : sts) {
	      llvm::errs() << "\r" << ++j;
	    if (ss.contains(st.s) && ts.contains(st.t)) {
	      const Edge e = {.src = src, .dst = dst};
	      results[e].insert(st);
	    }
	  }
	}
      }
      llvm::errs() << "\n";

      return results;
      
    }

    
#else
    struct InOut {
      llvm::BitVector in;
      llvm::BitVector out;

      bool operator==(const InOut& o) const {
	return in == o.in && out == o.out;
      }
      bool operator!=(const InOut& o) const {
	return !(*this == o);
      }
    };

    static void computeReachingInit(const std::vector<Node>& nodes,
				    const llvm::BitVector& empty,
				    std::map<Node, InOut>& map) {
      for (const Node& node : nodes) {
	InOut& v = map[node];
	v.out = v.in = empty;
      }
    }

    static void computeReachingStep(std::map<Node, llvm::SmallSet<Node, 2>>& G,
				    const std::map<Node, llvm::BitVector>& addins,
				    const std::vector<Node>& order,
				    std::map<Node, InOut>& map) {
      std::map<Node, InOut> bak;
      do {
	bak = map;
	for (const Node& dst : order) {
	  InOut& v = map.at(dst);
	  for (const Node& src : G[dst])
	    v.in |= map.at(src).out;
	  v.out = v.in;
	  v.out |= addins.at(dst);
	}
      } while (map != bak);
    }

    std::map<Node, InOut> fwd, bwd;
    std::vector<Node> fwd_order, bwd_order;
    std::vector<std::pair<Edge, llvm::BitVector>> computeReaching() {
      auto& sts = this->sts;
      
      std::map<ST, size_t> sts_map;
      for (const ST& st : sts)
	sts_map.emplace(st, sts_map.size());

      std::vector<std::pair<Edge, llvm::BitVector>> results; // really, can just return count.

      std::map<Node, llvm::SmallSet<Node, 2>> G, Grev;
      for (const auto& [src, dsts] : this->G) {
	for (const auto& [dst, _] : dsts) {
	  G[src].insert(dst);
	  Grev[dst].insert(src);
	}
      }

      if (G.empty())
	return {};
      
      const auto st_to_idx = [&] (const ST& st) {
	return sts_map.at(st);
      };
      const auto num_sts = sts.size();

      std::map<Node, llvm::BitVector> sts_to_set;
      for (const Node& node : getNodes())
	sts_to_set.emplace(node, llvm::BitVector(num_sts, false));
      for (const ST& st : sts) {
	const auto st_idx = st_to_idx(st);
	sts_to_set[st.s].set(st_idx);
	sts_to_set[st.t].set(st_idx);
      }

      const llvm::BitVector empty(num_sts, false);
      if (bwd_order.empty() || true)
	bwd_order = topological_order(Grev);
      if (fwd_order.empty() || true)
	fwd_order = topological_order(G);
      if (fwd.empty() || bwd.empty() || true) {
	computeReachingInit(fwd_order, empty, fwd);
	computeReachingInit(bwd_order, empty, bwd);
      }
      computeReachingStep(G, sts_to_set, fwd_order, fwd);
      computeReachingStep(Grev, sts_to_set, bwd_order, bwd);

      // Find shared sts between edges
      for (const auto& [src, dsts] : G) {
	const llvm::BitVector& src_bv = fwd[src].out;
	for (const Node& dst : dsts) {
	  llvm::BitVector dst_bv = bwd[dst].out;
	  dst_bv &= src_bv;
	  const Edge e = {.src = src, .dst = dst};
	  results.emplace_back(e, std::move(dst_bv));
	}
      }
      return results;
    }
#endif
    
    template <class Func>
    void for_each_edge(Func func) const {
      for_each_edge_order(getNodes(), func);
    }

    template <class Func>
    void for_each_edge_order(const auto& order, Func func) const {
      for (const Node& src : order)
	for (const auto& [dst, _] : this->G[src])
	  func(src, dst);
    }

    static std::set<Node> getNodes(const std::map<Node, llvm::SmallSet<Node, 2>>& G) {
      std::set<Node> nodes;
      for (const auto& [src, dsts] : G) {
	nodes.insert(src);
	for (const Node& dst : dsts)
	  nodes.insert(dst);
      }
      return nodes;
    }

    std::set<Node> getNodes() const {
      std::set<Node> nodes;
      for (const auto& [src, dsts] : this->G) {
	nodes.insert(src);
	for (const auto& [dst, _] : dsts)
	  nodes.insert(dst);
      }
      return nodes;
    }

#if 1
    static std::vector<Node> postorder(std::map<Node, llvm::SmallSet<Node, 2>>& G) {
      std::set<Node> unseen = getNodes(G);
      std::set<Node> seen;
      std::vector<Node> order;

      while (!unseen.empty()) {
	// find good root node
	Node root;
	{
	  // get degrees
	  // NOTE: We know that any src nodes must also been in unseen.
	  std::map<Node, unsigned> degs_in;
	  for (const Node& src : unseen)
	    for (const Node& dst : G[src])
	      if (unseen.contains(dst))
		degs_in[dst]++;
	  if (degs_in.empty())
	    root = *unseen.begin();
	  else
	    root = degs_in.begin()->first;
	}
	assert(unseen.contains(root));

	// now do DFS
	std::stack<Node> stack;
	const auto push = [&] (const Node& node) -> bool {
	  if (unseen.erase(node)) {
	    seen.insert(node);
	    stack.push(node);
	    return true;
	  } else {
	    return false;
	  }
	};
	push(root);
	assert(!unseen.contains(root));

	while (!stack.empty()) {
	  Node src = stack.top();
	  bool found_unseen = false;
	  for (const Node& dst : G[src])
	    found_unseen |= push(dst);
	  if (!found_unseen) {
	    order.push_back(src);
	    stack.pop();
	  }
	}
      }
      assert(llvm::equal(getNodes(G), std::set<Node>(order.begin(), order.end())));
      return order;
    }
#else
    static std::vector<Node> postorder() {
      std::stack<Node> stack;
      std::set<Node> seen;
      std::vector<Node> order;

      // try to find a good start node
      {
	// get degrees
	std::map<Node, unsigned> degrees;
	for (const Node& node : getNodes())
	  degrees[node] = 0;
	for_each_edge([&degrees] (const Node&, const Node& dst) {
	  degrees[dst]++;
	});

	if (degrees.empty())
	  return {};

	std::map<unsigned, std::set<Node>> degrees_r;
	for (const auto& [node, deg] : degrees)
	  degrees_r[deg].insert(node);

	for (const auto& node : degrees_r.begin()->second)
	  stack.push(node);
      }

      while (!stack.empty()) {
	const Node node = stack.top();
	if (seen.insert(node).second) {
	  for (const auto& [dst, _] : this->G[node]) {
	    if (!seen.contains(dst))
	      stack.push(dst);
	  }
	} else {
	  order.push_back(node);
	  stack.pop();
	}
      }

      if (!llvm::equal(getNodes(), std::set<Node>(order.begin(), order.end()))) {
	const auto nodes = getNodes();
	for (const Node& node : getNodes()) {
	  if (!order.contains(node))
	    errs() << "node: " << *node.V << "\n";
	}
	  
      }

      assert(llvm::equal(getNodes(), std::set<Node>(order.begin(), order.end())));

      return order;
    }
#endif

    static std::vector<Node> topological_order(std::map<Node, llvm::SmallSet<Node, 2>>& G) {
      auto order = postorder(G);
      llvm::reverse(order);
      return order;
    }
    
  };

}
