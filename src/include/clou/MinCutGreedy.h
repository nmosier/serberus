#pragma once

#include "MinCutBase.h"

#include <queue>

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

      changed = true;
      while (changed) {
	const auto bak = fwd;
	changed = false;
	for (const auto& [src, dsts] : this->G) {
	  for (const auto& [dst, _] : dsts) {
	    const auto& in = fwd[src];
	    auto& out = fwd[dst];
	    changed |= bvand(in, bvnot(out)).any();
	    out |= in;
	  }
	}
	changed = (bak != fwd);
      }

      changed = true;
      while (changed) {
	const auto bak = bwd;
	changed = false;
	for (const auto& [src, dsts] : this->G) {
	  for (const auto& [dst, _] : dsts) {
	    const auto& in = bwd[dst];
	    auto& out = bwd[src];
	    changed |= bvand(in, bvnot(out)).any();
	    out |= in;
	  }
	}
	changed = (bak != bwd);
      }

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
  };

}
