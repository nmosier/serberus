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
      
	Weight maxw = 0;
	Edge maxe;
	for (const auto& [e, cost] : getEdges()) {
	  const unsigned paths = computePaths(e);
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
  };

}
