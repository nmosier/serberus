#pragma once

#include <z3++.h>
#include <llvm/ADT/StringRef.h>

#include "MinCutBase.h"

namespace clou {

  template <class Node, class Weight>
  class MinCutSMT final : public MinCutBase<Node, Weight> {
  public:
    using Super = MinCutBase<Node, Weight>;
    using Graph = Super::Graph;
    using Edge = Super::Edge;
    using ST = Super::ST;
    
    void run() override {
      // Name nodes
      std::map<Node, size_t> ids;
      for (const auto& [src, dsts] : this->G) {
	ids.emplace(src, ids.size());
	for (const auto& [dst, _] : dsts) {
	  ids.emplace(dst, ids.size());
	}
      }

      // Reverse graph
      Graph Grev;
      for (const auto& [src, dsts] : this->G) {
	for (const auto& [dst, weight] : dsts) {
	  Grev[dst][src] = weight;
	}
      }

      z3::context ctx;
      z3::expr empty_set = z3::empty_set(ctx.int_sort());
      z3::sort set_sort = empty_set.get_sort();

      // Variable name generators
      const auto node_name = [&ids] (const Node& node, llvm::StringRef suffix) -> std::string {
	std::stringstream ss;
	ss << ids.at(node) << "-" << suffix.str();
	return ss.str();
      };

      const auto edge_name = [&ids] (const Edge& edge, llvm::StringRef suffix) -> std::string {
	std::stringstream ss;
	ss << ids.at(edge.src) << "-" << ids.at(edge.dst) << "-" << suffix.str();
	return ss.str();
      };

      const auto set_in_var = [&] (const Node& node) -> z3::expr {
	return ctx.constant(node_name(node, "set-in").c_str(), set_sort);
      };

      const auto set_out_var = [&] (const Node& node) -> z3::expr {
	return ctx.constant(node_name(node, "set-out").c_str(), set_sort);
      };

      const auto edge_cut_var = [&] (const Edge& edge) -> z3::expr {
	return ctx.bool_const(edge_name(edge, "cut").c_str());
      };

      // Collect sources and transmitters
      std::set<Node> sources, transmitters;
      for (const ST& st : this->sts) {
	sources.insert(st.s);
	transmitters.insert(st.t);
      }
      
      // Construct graph
      z3::optimize solver(ctx);

      // Define set-in's
      for (const auto& [dst, _] : ids) {
	// Define set_in
	z3::expr set_in = empty_set;
	for (const auto& [src, _] : Grev[dst]) {
	  const z3::expr cut = edge_cut_var({.src = src, .dst = dst});
	  const z3::expr src_set_out = z3::ite(cut, empty_set, set_out_var(src));
	  set_in = z3::set_union(set_in, src_set_out);
	}
	solver.add(set_in == set_in_var(dst));
      }

      // Define set-out's
      for (const auto& [node, id] : ids) {
	z3::expr set_out = set_in_var(node);
	if (sources.contains(node)) {
	  set_out = z3::set_add(set_out, ctx.int_val(id));
	}
	solver.add(set_out == set_out_var(node));
      }

      // Assert that transmitters never recieve a set containing their sources
      for (const ST& st : this->sts) {
	solver.add(!z3::set_member(ctx.int_val(ids.at(st.s)), set_in_var(st.t)));
      }

      // Minimize cut weights
      z3::expr_vector cut_weights(ctx);
      cut_weights.push_back(ctx.int_val(0));
      for (const auto& [src, dsts] : this->G) {
	for (const auto& [dst, weight] : dsts) {
	  cut_weights.push_back(z3::ite(edge_cut_var({.src = src, .dst = dst}),
					ctx.int_val(weight),
					ctx.int_val(0)));
	}
      }
      solver.minimize(z3::sum(cut_weights));

      const z3::check_result check_res = solver.check();
      switch (check_res) {
      case z3::sat:
	break;

      case z3::unsat:
      case z3::unknown:
	std::abort();
      }

      z3::model model = solver.get_model();

      // Collect cut edges
      for (const auto& [src, dsts] : this->G) {
	for (const auto& [dst, _] : dsts) {
	  if (model.eval(edge_cut_var({.src = src, .dst = dst})).is_true()){
	    this->cut_edges.push_back({.src = src, .dst = dst});
	  }
	}
      }
    }
  };
  
}
