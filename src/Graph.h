#pragma once

#include <set>
#include <map>

#include <llvm/ADT/STLExtras.h>

namespace clou {

  template <class Node, class Weight>
  class Graph {
  public:
    using Set = std::set<Node>;
    using Map = std::map<Node, std::map<Node, Weight>>;

    struct Edge {
      Node src;
      Node dst;
      bool operator==(const Edge& o) const { return src == o.src && dst == o.dst; }
      bool operator!=(const Edge& o) const { return !(*this == o); }
    };

    Set nodes;
    Map fwd;
    Map rev;

    void addNode(const Node& node) {
      nodes_.insert(node);
    }

    void addEdge(const Node& src, const Node& dst, Weight weight) {
      addNode(src);
      addNode(dst);
      rev[dst][src] = fwd[src][dst] = weight;
    }

    void getBasicBlocks(OutputIt out) const {
      BasicBlock block;

      std::set<Node> seen;

      for (Node node : nodes) {
	if (seen.insert(node).second) {
	  std::vector<Node> block = {node};
	  
	  if (fwd.at(node).size() == 1 && rev.at(node).size() != ) {
	    Weight weight = fwd.at(node).begin()->second;
	    while (true) {
	      const auto& succs = fwd.at(node);
	      if (succs.size() != 1) { break; }
	      const auto& [succ, succ_weight] = *succs.begin();
	      if (weight != succ_weight) { break; }
	      if (rev.at(succ).size() != 1) { break; }
	      block.push_back(succ);
	      node = succ;
	    }
	  }
	  *out++ = std::move(block);
	}
      }
    }
  };
    
}
