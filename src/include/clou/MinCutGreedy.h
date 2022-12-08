#pragma once

#include "MinCutBase.h"

#include <queue>

namespace clou {

  template <class Node, class Weight>
  class MinCutGreedy final : public MinCutBase<Node, Weight> {
  public:
    using Super = MinCutBase<Node, Weight>;
    using Edge = typename Super::Edge;
    using ST = typename Super::ST;

    void run() override {
      unsigned i = 0;
      while (true) {
	++i;
	llvm::errs() << "start " << i << "\n";

	auto reaching_sts = computeReaching();
      
	Weight maxw = 0;
	Edge maxe;
	for (const auto& [e, cost] : getEdges()) {
	  const unsigned paths = reaching_sts[e].size();
	  const float w = paths * (1.f / static_cast<float>(cost));
	  if (w > maxw) {
	    maxw = w;
	    maxe = e;
	  }
	}

	llvm::errs() << "stop " << i << "\n";

	// if no weights were > 0, we're done
	if (maxw == 0)
	  break;

	// cut the max edge and continue
	cutEdge(maxe);
	llvm::errs() << "cut " << i << "\n";
      }
    }
    
  private:
    using EdgeSet = std::set<Edge>;

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

#if 0
    // want to check if path exists between two nodes.
    bool pathExists(const Node& u, const Node& v) const {
      std::set<Node> seen;
      std::queue<Node> todo;
      todo.push(u);

      while (!todo.empty()) {
	const Node w = todo.front();
	todo.pop();

	if (w == v)
	  return true;

	if (seen.insert(w).second)
	  for (const auto& [dst, _] : this->G[w])
	    todo.push(dst);
      }

      return false;
    }
    
    unsigned computePaths(const Edge& e) const {
      return llvm::count_if(this->sts, [&] (const ST& st) {
	return pathExists(st.s, e.src) && pathExists(e.dst, st.t);
      });
    }
#endif

    std::map<Edge, std::set<ST>> computeReaching() const {
      std::map<Node, std::set<ST>> fwd, bwd, both;
      bool changed;
      
      for (const ST& st : this->sts)
	fwd[st.s].insert(st);
      changed = true;
      while (changed) {
	changed = false;
	for (const auto& [src, dsts] : this->G) {
	  for (const auto& [dst, _] : dsts) {
	    const auto& in = fwd[src];
	    auto& out = fwd[dst];
	    for (const auto& x : in)
	      changed |= out.insert(x).second;
	  }
	}
      }

      for (const ST& st : this->sts)
	bwd[st.t].insert(st);
      changed = true;
      while (changed) {
	changed = false;
	for (const auto& [src, dsts] : this->G) {
	  for (const auto& [dst, _] : dsts) {
	    const auto& in = bwd[dst];
	    auto& out = bwd[src];
	    for (const auto& x : in)
	      changed |= out.insert(x).second;
	  }
	}
      }
      
      for (const auto& [node, _] : this->G) {
	const auto& sources = fwd[node];
	const auto& sinks = bwd[node];
	auto& out = both[node];
	std::set_intersection(sources.begin(), sources.end(), sinks.begin(), sinks.end(), std::inserter(out, out.end()));
      }

      // finally, get edges
      std::map<Edge, std::set<ST>> results;
      for (const auto& [src, dsts] : this->G) {
	for (const auto& [dst, _] : dsts) {
	  const auto& a = both[src];
	  const auto& b = both[dst];
	  auto& out = results[{.src = src, .dst = dst}];
	  std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::inserter(out, out.end()));
	}
      }

      return results;
    }
  };

}
