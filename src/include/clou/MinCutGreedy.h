#pragma once

#include "MinCutBase.h"

#include <queue>
#include <stack>
#include <unordered_map>
#include <map>
#include <set>

#include <llvm/ADT/SmallSet.h>

#define DEBUG(...) ;

namespace clou {

  template <class Node, class Weight, class NodeHash>
  class MinCutGreedy final : public MinCutBase<Node, Weight> {
  public:
    using Super = MinCutBase<Node, Weight>;
    using Edge = typename Super::Edge;
    using ST = typename Super::ST;

    struct EdgeHash {
      NodeHash node_hash;
      EdgeHash(NodeHash node_hash = NodeHash()): node_hash(node_hash) {}
      auto operator()(const Edge& e) const {
	return llvm::hash_combine(node_hash(e.src), node_hash(e.dst));
      }
    };

    void run() override {
      // sort and de-duplicate sts
      {
	const std::set<ST> st_set(this->sts.begin(), this->sts.end());
	this->sts.resize(st_set.size());
	llvm::copy(st_set, this->sts.begin());
      }

      DEBUG(llvm::errs() << "min-cut on " << getNodes().size() << " nodes\n");
      
      unsigned i = 0;
      while (true) {
	++i;
	DEBUG(llvm::errs() << "\titeration " << i << "\n");

	auto reaching_sts = computeReaching();
      
	float maxw = 0;
	Edge maxe;
	llvm::BitVector maxbv;

	for (const auto& [e, cost] : getEdges()) {
	  const unsigned paths = reaching_sts[e];
	  const float w = paths * (1.f / static_cast<float>(cost));
	  if (w > maxw) {
	    maxw = w;
	    maxe = e;
	  }
	}

	// if no weights were > 0, we're done
	if (maxw == 0)
	  break;

	// cut the max edge and continue
	cutEdge(maxe);
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

    mutable std::set<ST> disconnected_sts;
    std::map<Edge, unsigned> computeReaching() {
      std::vector<Node> nodevec;
      llvm::copy(getNodes(), std::back_inserter(nodevec));
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
	const auto src_idx = node_to_idx(src);
	for (const auto& [dst, _] : dsts) {
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

      for ([[maybe_unused]] int i = 0; const auto& st : sts) {
	DEBUG(llvm::errs() << "\r\t\tsts processed: " << i++ << "/" << sts.size());
#if 0
	if (disconnected_sts.contains(st))
	  continue;
#endif
	const auto& fwd = s_reach.at(st.s);
	const auto& bwd = t_reach.at(st.t);
	llvm::BitVector both = fwd;
	both &= bwd;
	if (both.count() < 2) {
#if 0
	  disconnected_sts.insert(st);
#endif
	  continue;
	}
	for (auto src_idx : both.set_bits()) {
	  for (auto dst_idx : G[src_idx]) {
	    if (both.test(dst_idx)) {
	      auto& result = results[src_idx][dst_idx];
	      ++result;
	    }
	  }
	}
      }

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
