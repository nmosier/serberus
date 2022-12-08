#pragma once

#include <vector>
#include <map>
#include <set>
#include <queue>
#include <iostream>
#include <cassert>

#include <llvm/ADT/iterator_range.h>

namespace clou {

template <class Node, class Weight>
class MinCutBase {
public:
  struct ST {
    Node s;
    Node t;

    auto pair() const {
      return std::make_pair(s, t);
    }

    bool operator==(const ST& o) const { return pair() == o.pair(); }
    bool operator!=(const ST& o) const { return !(*this == o); }
    bool operator<(const ST& o) const { return pair() < o.pair(); }
  };
  
  using Graph = std::map<Node, std::map<Node, Weight>>;

  struct Edge {
    Node src;
    Node dst;

    auto pair() const {
      return std::make_pair(src, dst);
    }

    bool operator==(const Edge& o) const { return pair() == o.pair(); }
    bool operator!=(const Edge& o) const { return !(*this == o); }
    bool operator<(const Edge& o) const { return pair() < o.pair(); }
  };

  mutable Graph G;
  std::vector<ST> sts;
  std::vector<Edge> cut_edges;

  void add_st(const ST& st) {
    sts.push_back(st);
  }

  void add_st(const Node& s, const Node& t) {
    sts.push_back({.s = s, .t = t});
  }
 
  template <class IteratorT1, class IteratorT2>
  void add_st(llvm::iterator_range<IteratorT1> sources,
	      llvm::iterator_range<IteratorT2> transmitters) {
    for (const Node& s : sources) {
      for (const Node& t : transmitters) {
	add_st(s, t);
      }
    }
  }

  virtual void run() = 0;
  
private:
};

  // TODO: Move this to source file.
  extern bool optimized_min_cut;

}
