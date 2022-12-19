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
      
      unsigned i = 0;
      while (true) {
	++i;

	auto reaching_sts = computeReaching();
      
	float maxw = 0;
	Edge maxe;
	for (const auto& [e, cost] : getEdges()) {
	  const unsigned paths = reaching_sts[e].size();
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

    std::set<Node> bfs(const Node& root, const Node& target, std::map<Node, std::set<Node>>& G) const {
      std::stack<Node> todo;
      std::set<Node> seen;
      todo.push(root);
      while (!todo.empty()) {
	const Node node = todo.top();
	todo.pop();
	if (seen.insert(node).second && node != target) {
	  // add successors
	  for (const Node& dst : G[node])
	    todo.push(dst);
	}
      }
      return seen;
    }

    std::map<Edge, std::set<ST>> computeReaching() const {
      auto& sts = this->sts;

      std::map<Edge, std::set<ST>> results;

      /* Approach:
       * BFS for each s-t pair.
       *
       */

      std::map<Node, std::set<Node>> G, Grev;
      for (const auto& [src, dsts] : this->G) {
	for (const auto& [dst, _] : dsts) {
	  G[src].insert(dst);
	  Grev[dst].insert(src);
	}
      }

      if (G.empty())
	return {};
      
      for (const ST& st : sts) {
	std::set<Node> fwd, bwd;
	fwd = bfs(st.s, st.t, G);
	if (fwd.contains(st.t))
	  bwd = bfs(st.t, st.s, Grev);
	if (fwd.empty() || bwd.empty())
	  continue;
	std::set<Node> both;
	std::set_intersection(fwd.begin(), fwd.end(), bwd.begin(), bwd.end(), std::inserter(both, both.end()));
	for (const Node& src : both) {
	  for (const Node& dst : G[src]) {
	    if (both.contains(dst)) {
	      const Edge edge = {.src = src, .dst = dst};
	      results[edge].insert(st);
	    }
	  }
	}
      }

      return results;
    }
    
#if 0
    std::map<Edge, std::set<ST>> computeReaching() const {
      auto& sts = this->sts;
      assert(llvm::is_sorted(sts));

      const auto getIndex = [&sts] (const ST& st) -> unsigned {
	const auto it = std::lower_bound(sts.begin(), sts.end(), st);
	assert(it != sts.end());
	assert(*it == st);
	return it - sts.begin();
      };

      const auto getST = [&sts] (unsigned idx) -> const ST& {
	assert(idx < sts.size());
	return sts[idx];
      };

      const llvm::BitVector emptyset(sts.size());
      
      std::map<Node, llvm::BitVector> fwd, bwd, both;
      bool changed;

      std::set<Node> nodes;
      for (const auto& [src, dsts] : this->G) {
	nodes.insert(src);
	for (const auto& [dst, _] : dsts) {
	  nodes.insert(dst);
	}
      }

      for (const Node& v : nodes) {
	fwd[v] = bwd[v] = both[v] = emptyset;
      }

      for (const ST& st : sts) {
	fwd[st.s].set(getIndex(st));
	bwd[st.t].set(getIndex(st));
      }

      std::map<Node, std::set<Node>> Grev;
      for (const auto& [src, dsts] : this->G)
	for (const auto& [dst, _] : dsts)
	  Grev[dst].insert(src);
      
      auto order = topological_order();
      do {
	changed = false;
	for (const Node& dst : order) {
	  auto& out = fwd[dst];
	  const auto bak = out;
	  for (const Node& src : Grev[dst]) {
	    const auto& in = fwd[src];
	    out |= in;
	  }
	  changed |= (out != bak);
	}
      } while (changed);

      llvm::reverse(order);
      do {
	changed = false;
	for (const Node& src : order) {
	  auto& out = bwd[src];
	  const auto bak = out;
	  for (const auto& [dst, _] : this->G[src]) {
	    const auto& in = bwd[dst];
	    out |= in;
	  }
	  changed |= (out != bak);
	}
      } while (changed);

      for (const ST& st : sts) {
	assert(fwd[st.s].test(getIndex(st)));
	assert(bwd[st.t].test(getIndex(st)));
      }

      for (const Node& node : nodes) {
	const auto& sources = fwd[node];
	const auto& sinks = bwd[node];
	both[node] = bvand(sources, sinks);
      }

      // finally, get edges
      std::map<Edge, std::set<ST>> results;
      for (const auto& [src, dsts] : this->G) {
	for (const auto& [dst, _] : dsts) {
	  const auto& a = both[src];
	  const auto& b = both[dst];
	  auto& out = results[{.src = src, .dst = dst}];
	  llvm::BitVector out_bv = bvand(a, b);
	  for (unsigned bit : out_bv.set_bits())
	    out.insert(getST(bit));
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

    std::set<Node> getNodes() const {
      std::set<Node> nodes;
      for (const auto& [src, dsts] : this->G) {
	nodes.insert(src);
	for (const auto& [dst, _] : dsts)
	  nodes.insert(dst);
      }
      return nodes;
    }


    std::vector<Node> postorder() const {
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

      assert(llvm::equal(getNodes(), std::set<Node>(order.begin(), order.end())));

      return order;
    }

    std::vector<Node> topological_order() const {
      auto order = postorder();
      llvm::reverse(order);
      return order;
    }
    
  };

}
