#pragma once

#include <vector>
#include <map>
#include <set>
#include <queue>
#include <iostream>
#include <cassert>

#include <llvm/ADT/iterator_range.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/WithColor.h>
#include <llvm/ADT/ArrayRef.h>

namespace clou {

template <class Node, class Weight>
class MinCutBase {
public:
  struct ST {
    std::vector<std::set<Node>> waypoints;
    auto operator<=>(const ST& o) const = default;
  };
  
  using Graph = std::map<Node, std::map<Node, Weight>>;

  struct Edge {
    Node src;
    Node dst;
    auto operator<=>(const Edge&) const = default;
  };

  mutable Graph G;
protected:
  std::vector<ST> sts;
public:
  std::vector<Edge> cut_edges;

  llvm::ArrayRef<ST> get_sts() const { return sts; }

  void add_st(std::initializer_list<std::initializer_list<Node>> init_st) {
    ST& st = sts.emplace_back();
    for (std::initializer_list<Node> init_set : init_st) {
      assert(init_set.size() >= 1);
      st.waypoints.emplace_back(init_set);
      assert(st.waypoints.back().size() >= 1);
    }
    assert(st.waypoints.size() >= 2);
  }

  virtual void run() = 0;
  
private:
};

}
