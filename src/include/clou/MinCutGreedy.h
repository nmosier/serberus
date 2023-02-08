#pragma once

#include "MinCutBase.h"
#include "clou/FordFulkerson.h"

#include <queue>
#include <stack>
#include <unordered_map>
#include <map>
#include <set>

#include <llvm/ADT/SmallSet.h>
#include <llvm/Clou/Clou.h>
#include <llvm/ADT/STLExtras.h>

#define DEBUG(...) ;

namespace clou {

  template <class Node>
  class MinCutGreedy final : public MinCutBase<Node, unsigned> {
  public:
    using Weight = unsigned;
    using Super = MinCutBase<Node, Weight>;
    using Edge = typename Super::Edge;
    using ST = typename Super::ST;

  private:
    using Idx = unsigned;
    struct IdxST {
      std::vector<std::set<Idx>> waypoints;
      template <typename... Ts> IdxST(Ts&&... args): waypoints(std::forward<Ts>(args)...) {}
      auto operator<=>(const IdxST&) const = default;
    };
    struct IdxEdge {
      Idx src, dst;
      auto operator<=>(const IdxEdge& o) const = default;
    };    
    using IdxGraph = std::vector<std::map<Idx, Weight>>;
    
  public:

    void run() override {
      // sort and de-duplicate sts
      {
	const std::set<ST> st_set(this->sts.begin(), this->sts.end());
	this->sts.resize(st_set.size());
	llvm::copy(st_set, this->sts.begin());
      }

      llvm::errs() << "min-cut on " << getNodes().size() << " nodes\n";

      /* New algorithm:
       * Maintain a set of LFENCE insertion points.
       * For each ST pair:
       *   Perform the optimal min cut.
       */

      // Convert everything into efficient representation.
      using Idx = unsigned;

      // Get nodes.
      std::vector<Node> nodes;
      llvm::copy(getNodes(), std::back_inserter(nodes));
      const auto node_to_idx = [&nodes] (const Node& node) -> Idx {
	const auto it = llvm::lower_bound(nodes, node);
	assert(it != nodes.end() && *it == node);
	return it - nodes.begin();
      };
      const auto idx_to_node = [&nodes] (Idx idx) -> const Node& {
	assert(idx < nodes.size());
	return nodes[idx];
      };

      // Get index graph.
      IdxGraph G(nodes.size());
      for (const auto& [src, dsts] : this->G) {
	const Idx isrc = node_to_idx(src);
	auto& idsts = G[isrc];
	for (const auto& [dst, w] : dsts) {
	  const Idx idst = node_to_idx(dst);
	  idsts[idst] = w;
	}
      }

      // Get index sts.
      std::vector<IdxST> sts;
      for (const ST& st : this->sts) {
	auto& ist = sts.emplace_back();
	for (const auto& way : st.waypoints) {
	  auto& iway = ist.waypoints.emplace_back();
	  llvm::transform(way, std::inserter(iway, iway.end()), node_to_idx);
	}
      }

      bool changed;
      using Cuts = std::vector<std::vector<IdxEdge>>;
      using CutsHistory = std::set<Cuts>;
      Cuts cuts(sts.size());
      CutsHistory cuts_hist;
      const IdxGraph OrigG = G; // mainly for checking weights
      int num_iterations = 0;
      do {
	llvm::errs() << "\rnum_iterations: " << ++num_iterations;
	
	changed = false;

	for (const auto& [st, cut] : llvm::zip(sts, cuts)) {
	  
	  // Add old cut edges back in to graph.
	  const std::vector<IdxEdge> oldcut = std::move(cut);
	  for (const IdxEdge& e : oldcut) {
	    const Weight w = OrigG[e.src].at(e.dst);
	    [[maybe_unused]] const bool inserted = G[e.src].insert(std::make_pair(e.dst, w)).second;
	    assert(inserted && "Cut edge still in graph G!");
	  }

	  // Compute new local min cut.
	  const auto newcut_tmp = ford_fulkerson_multi(G, st.waypoints);
	  std::vector<IdxEdge> newcut(newcut_tmp.size());
	  llvm::transform(newcut_tmp, newcut.begin(), [] (const auto& p) -> IdxEdge {
	    return {.src = p.first, .dst = p.second};
	  });

#if CHECK_CUTS
	  {
	    std::set<IdxEdge> cutset;
	    llvm::copy(newcut, std::inserter(cutset, cutset.end()));
	    checkCutST(st.waypoints, cutset, G);
	  }
#endif

	  // Remove new cut edges from G.
	  for (const IdxEdge& e : newcut) {
	    assert(G.at(e.src).find(e.dst) != G.at(e.src).end());
	    [[maybe_unused]] const auto erased = G[e.src].erase(e.dst);
	    assert(erased > 0);
	  }

	  // Check if changed.
	  if (oldcut != newcut)
	    changed = true;

	  // Update cut.
	  cut = std::move(newcut);

#if CHECK_CUTS
	  {
	    std::set<IdxEdge> cutset;
	    for (const auto& cutvec : cuts)
	      llvm::copy(cutvec, std::inserter(cutset, cutset.end()));
	    checkCutST(st.waypoints, cutset, OrigG);
	  }
#endif	  
	}

	if (changed) {
	  if (!cuts_hist.insert(cuts).second) {
	    llvm::WithColor::warning() << "detected loop in min-cut algorithm\n";
	    break;
	  }
	}
	
      } while (changed);
      llvm::errs() << "\n";

#if CHECK_CUTS
      checkCut(cuts, sts, OrigG);
#endif
      

      // Now add all cut edges to master copy.
      for (const auto& cut : cuts)
	for (const IdxEdge& e : cut)
	  this->cut_edges.push_back({.src = idx_to_node(e.src), .dst = idx_to_node(e.dst)});
    }
    
  private:
    using EdgeSet = std::set<Edge>;

    static void checkCutST(llvm::ArrayRef<std::set<unsigned>> st, const std::set<IdxEdge>& cut, const IdxGraph& G) {
      assert(st.size() >= 2);
      const auto succs = [&] (unsigned u) {
	std::set<unsigned> succs;
	for (const auto& [v, w] : G.at(u)) {
	  assert(w > 0);
	  const IdxEdge e = {.src = u, .dst = v};
	  if (!cut.contains(e))
	    succs.insert(v);
	}
	return succs;
      };

      std::vector<std::set<unsigned>> particles;
      std::vector<std::map<unsigned, unsigned>> parents;
      std::set<unsigned> S = st.front();
      for (const std::set<unsigned>& T : st.drop_front()) {
	auto& parent = parents.emplace_back();
	particles.push_back(S);

	// Find all nodes reachable from S.
	std::set<unsigned> reach;
	std::stack<unsigned> todo;
	for (unsigned u : S)
	  todo.push(u);

	while (!todo.empty()) {
	  const unsigned u = todo.top();
	  todo.pop();
	  for (unsigned v : succs(u)) {
	    if (reach.insert(v).second) {
	      todo.push(v);
	      parent[v] = u;
	    }
	  }
	}

	S.clear();
	std::set_intersection(T.begin(), T.end(), reach.begin(), reach.end(), std::inserter(S, S.end()));
      }
      particles.push_back(S);
      
      if (!S.empty()) {
	llvm::WithColor::error() << "found s-t path in MinCutGreedy\n";
	std::abort();
      }
    }

    static void checkCut(llvm::ArrayRef<std::vector<IdxEdge>> cut, llvm::ArrayRef<IdxST> sts, const IdxGraph& G) {
      std::set<IdxEdge> cutset;
      for (const auto& cutvec : cut)
	llvm::copy(cutvec, std::inserter(cutset, cutset.end()));
      for (const IdxST& st : sts)
	checkCutST(st.waypoints, cutset, G);
    }

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

    void cutEdge(const Edge& e) {
      this->cut_edges.push_back(e);
      this->G[e.src].erase(e.dst);
      this->G[e.dst].erase(e.src);
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

    llvm::BitVector bfs(unsigned root, unsigned target, const std::vector<llvm::SmallSet<unsigned, 4>>& G, const llvm::BitVector& empty) const {
      std::stack<unsigned> todo;
      llvm::BitVector seen = empty;
      todo.push(root);
      while (!todo.empty()) {
	const unsigned node = todo.top();
	todo.pop();
	if (seen.test(node))
	  continue;
	seen.set(node);
	if (root == target || node != target)
	  for (const unsigned dst : G[node])
	    todo.push(dst);
      }
      return seen;
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

    mutable std::set<ST> disconnected_sts;
    mutable std::set<Node> disconnected_nodes;
    std::map<Edge, unsigned> computeReaching() {
      disconnected_nodes.clear();
      std::vector<Node> nodevec;
      llvm::copy(getNodes(), std::back_inserter(nodevec));
      llvm::erase_if(nodevec, [&] (const Node& u) { return disconnected_nodes.contains(u); });
      const auto n = nodevec.size();
      assert(llvm::is_sorted(nodevec));
      const auto node_to_idx = [&nodevec] (const Node& n) {
	const auto it = llvm::lower_bound(nodevec, n);
	assert(it != nodevec.end() && *it == n);
	return it - nodevec.begin();
      };
      const auto idx_to_node = [&nodevec] (auto idx) -> const Node& {
	assert(idx >= 0 && idx < nodevec.size());
	return nodevec[idx];
      };      

      
      struct IdxST {
	unsigned s, t;
      };
      std::vector<IdxST> sts;
      for (const ST& st : this->sts) {
	if (disconnected_sts.contains(st))
	  continue;
	IdxST st_;
	st_.s = node_to_idx(st.s);
	st_.t = node_to_idx(st.t);
	sts.push_back(st_);
      }

      std::vector<std::map<unsigned, unsigned>> results(n); // src_idx -> dst_idx -> count

      /* Approach:
       * BFS for each s-t pair.
       
       */

      const llvm::BitVector empty(n, false);

      std::vector<llvm::SmallSet<unsigned, 4>> G(n), Grev(n);
      for (const auto& [src, dsts] : this->G) {
	if (disconnected_nodes.contains(src))
	  continue;
	const auto src_idx = node_to_idx(src);
	for (const auto& [dst, _] : dsts) {
	  if (disconnected_nodes.contains(dst))
	    continue;
	  const auto dst_idx = node_to_idx(dst);
	  G[src_idx].insert(dst_idx);
	  Grev[dst_idx].insert(src_idx);
	}
      }

      if (G.empty())
	return {};      

      // We're recomputing a lot of information that we don't need to recompute.
      // What if for each unique source, we compute the set of nodes it reaches.
      // And do the same of each unique sink.
      // Then for each ST, we take the intersection

      llvm::BitVector ss = empty, ts = empty;
      for (const auto& st : sts) {
	ss.set(st.s);
	ts.set(st.t);
      }
      
      std::vector<llvm::BitVector> s_reach(n, empty), t_reach(n, empty);
      for (unsigned s : ss.set_bits()) {
	s_reach.at(s) = bfs(s, s, G, empty);
      }
      for (unsigned t : ts.set_bits()) {
	t_reach.at(t) = bfs(t, t, Grev, empty);
      }

      llvm::BitVector connected_nodes_bv;
      for ([[maybe_unused]] int i = 0; const auto& st : sts) {
	DEBUG(llvm::errs() << "\r\t\tsts processed: " << i++ << "/" << sts.size());
	const auto& fwd = s_reach.at(st.s);
	const auto& bwd = t_reach.at(st.t);
	llvm::BitVector both = fwd;
	both &= bwd;
	if (both.count() < 2) {
	  disconnected_sts.insert({.s = idx_to_node(st.s), .t = idx_to_node(st.t)});
	  continue;
	}
	connected_nodes_bv |= both;
	for (auto src_idx : both.set_bits()) {
	  for (auto dst_idx : G[src_idx]) {
	    if (both.test(dst_idx)) {
	      auto& result = results[src_idx][dst_idx];
	      ++result;
	    }
	  }
	}
      }

      llvm::BitVector disconnected_nodes_bv = connected_nodes_bv; disconnected_nodes_bv.flip();
      for (unsigned disconnected_node_idx : disconnected_nodes_bv.set_bits())
	disconnected_nodes.insert(idx_to_node(disconnected_node_idx));

      DEBUG(llvm::errs() << "\n");

      std::map<Edge, unsigned> results_;
      for (unsigned src_idx = 0; src_idx < n; ++src_idx) {
	const Node& src = idx_to_node(src_idx);
	Edge e;
	e.src = src;
	for (const auto& [dst_idx, count] : results[src_idx]) {
	  e.dst = idx_to_node(dst_idx);
	  results_[e] = count;
	}
      }

      return results_;
    }
    
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
