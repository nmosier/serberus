#pragma once

#include <map>
#include <vector>
#include <cstdlib>

#include <z3++.h>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/WithColor.h>

#include "MinCutBase.h"
#include "util.h"

namespace clou {

  constexpr bool mincut_optimize_collapse = true;

  template <class Node, class Weight>
  class MinCutSMT_Base : public MinCutBase<Node, Weight> {
  public:
    using Super = MinCutBase<Node, Weight>;
    using Graph = typename Super::Graph;
    using Edge = typename Super::Edge;
    using ST = typename Super::ST;

    void eraseNode(Graph& G, Graph& Grev, const Node& node) {
      for (const auto& [succ, _] : G[node]) {
	Grev[succ].erase(node);
      }
      for (const auto& [pred, _] : Grev[node]) {
	G[pred].erase(node);
      }
      G.erase(node);
      Grev.erase(node);
    }

    void removeEdge(Graph& G, Graph& Grev, const Node& src, const Node& dst) {
      G[src].erase(dst);
      Grev[dst].erase(src);
    }

    void simplifyGraph(Graph& G, Graph& Grev) {
      std::set<Node> sources, transmitters;
      for (const ST& st : this->sts) {
	sources.insert(st.s);
	transmitters.insert(st.t);
      }

      bool changed;
      do {
	changed = false;

	// Elide any nodes with singleton same-weight in and out edges
	for (const auto& [node, _] : G) {
	  if (!sources.contains(node) && !transmitters.contains(node)) {
	    auto& succs = G[node];
	    auto& preds = Grev[node];
	    if (succs.size() == 1 && preds.size() == 1) {
	      auto& [succ, succ_weight] = *succs.begin();
	      auto& [pred, pred_weight] = *preds.begin();
	      if (pred_weight == succ_weight) {
		const Weight weight = succ_weight;
		eraseNode(G, Grev, node);
		G[pred][succ] = weight;
		Grev[succ][pred] = weight;
		changed = true;
		break;
	      }
	    }
	  }
	}

	// Insert edges that we know must be placed
	for (const auto& [src, dsts] : G) {
	  for (const auto& [dst, _] : dsts) {
	    ST st = {.s = src, .t = dst};
	    if (std::find(this->sts.begin(), this->sts.end(), st) != this->sts.end()) {
	      removeEdge(G, Grev, src, dst);
	      this->cut_edges.push_back({.src = src, .dst = dst});
	      changed = true;
	      goto done;
	    }
	  }
	}
      done: ;
      } while (changed);
    }

    void run() final {
      if (this->sts.empty()) {
	return;
      }
      
      // Reverse graph
      Graph Grev;
      for (const auto& [src, dsts] : this->G) {
	for (const auto& [dst, weight] : dsts) {
	  Grev[dst][src] = weight;
	}
      }

      // Name nodes
      std::map<Node, std::size_t> ids;
      const auto add_id = [&] (const auto& node) {
	ids.emplace(node, ids.size());
      };
      for (const auto& [src, dsts] : this->G) {
	add_id(src);
	for (const auto& [dst, _] : dsts)
	  add_id(dst);
      }

      // Collect sources and transmitters
      std::map<Node, size_t> sources;
      assign_taint_ids(std::inserter(sources, sources.end()));
      std::set<size_t> source_ids;
      for (const auto& [_, source_id] : sources)
	source_ids.insert(source_id);
      std::set<Node> transmitters;
      for (const ST& st : this->sts) {
	transmitters.insert(st.t);
      }

      // for debugging
#if 0
      {
	llvm::errs() << "node mapping:\n";
	for (const auto& [node, id] : ids) {
	  llvm::errs() << "  " << node << " --> " << id << "\n";
	}
	llvm::errs() << "source index mapping:\n";
	for (const auto& [source, id] : sources) {
	  llvm::errs() << "  " << source << " --> " << id << "\n";
	}
      }
#endif
      
      z3::context ctx;
      z3::expr empty_set = get_empty_set(ctx, source_ids.size());
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
      
      // Construct graph
      z3::optimize solver(ctx);

      // Define set-in's
      for (const auto& [dst, _] : ids) {
	// Define set_in
	z3::expr set_in = empty_set;
	for (const auto& [src, _] : Grev[dst]) {
	  z3::expr cut = edge_cut_var({.src = src, .dst = dst});
	  z3::expr src_set_out = z3::ite(cut, empty_set, set_out_var(src));
	  set_in = set_union(set_in, src_set_out);
	}
	solver.add(set_in == set_in_var(dst));
      }

      // Define set-out's
      for (const auto& [node, _] : ids) {
	z3::expr set_out = set_in_var(node);
	if (sources.contains(node)) {
	  set_out = set_add(set_out, sources.at(node));
	}
	solver.add(set_out == set_out_var(node));
      }

      // Assert that transmitters never recieve a set containing their sources
      for (const ST& st : this->sts) {
	solver.add(!set_member(set_in_var(st.t), sources.at(st.s)));
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
#if 0
	for (const z3::expr& e : solver.assertions()) {
	  std::cerr << e.simplify() << "\n";
	}
#endif
	llvm::WithColor(llvm::errs(), llvm::HighlightColor::Error) << "internal error: Z3 solver query returned " << util::make_string_std(check_res) << "\n";
	std::_Exit(EXIT_FAILURE);
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

  protected:
    virtual z3::expr get_empty_set(z3::context& ctx, size_t n) const = 0;
    virtual z3::expr set_union(const z3::expr& a, const z3::expr& b) const = 0;
    virtual z3::expr set_add(const z3::expr& set, size_t i) const = 0;
    virtual z3::expr set_member(const z3::expr& set, size_t i) const = 0;

  private:
    template <class OutputIt>
    OutputIt assign_taint_ids_collapse(OutputIt out) {
      std::map<Node, std::set<Node>> s_ts, t_ss;
      for (const auto& st : this->sts) {
	s_ts[st.s].insert(st.t);
	t_ss[st.t].insert(st.s);
      }

      std::map<std::set<Node>, std::set<Node>> ts_to_ss, ss_to_ts;
      for (const auto& [s, ts] : s_ts)
	ts_to_ss[ts].insert(s);
      for (const auto& [t, ss] : t_ss)
	ss_to_ts[ss].insert(t);

      std::set<std::set<Node>> gsrcs, gsinks;
      for (const auto& [_, ss] : ts_to_ss)
	gsrcs.insert(ss);
      for (const auto& [_, ts] : ss_to_ts)
	gsinks.insert(ts);

      for (unsigned i = 0; const auto& gsrc : gsrcs) {
	for (const auto& src : gsrc)
	  *out++ = std::make_pair(src, i);
	++i;
      }

      // TODO: add validation?
      return out;
    }
    
    template <class OutputIt>
    OutputIt assign_taint_ids(OutputIt out) {
      if (mincut_optimize_collapse) {
	return assign_taint_ids_collapse(out);
      } else {
	std::set<Node> sources;
	for (const auto& st : this->sts)
	  sources.insert(st.s);
	for (unsigned i = 0; const auto& source : sources) {
	  *out++ = std::make_pair(source, i);
	  ++i;
	}
	return out;
      }
    }
  };

  template <class Node, class Weight>
  class MinCutSMT_Set final : public MinCutSMT_Base<Node, Weight> {
  private:
    z3::expr get_empty_set(z3::context& ctx, size_t) const final {
      return z3::empty_set(ctx.int_sort());
    }

    z3::expr set_union(const z3::expr& a, const z3::expr& b) const final {
      return z3::set_union(a, b);
    }

    z3::expr set_add(const z3::expr& set, size_t i) const final {
      return z3::set_add(set, set.ctx().int_val(i));
    }

    z3::expr set_member(const z3::expr& set, size_t i) const final {
      return z3::set_member(set.ctx().int_val(i), set);
    }
  };

  template <class Node, class Weight>
  class MinCutSMT_BV final : public MinCutSMT_Base<Node, Weight> {
  private:
    z3::expr get_singleton(const z3::expr& ref, size_t i) const {
      z3::sort sort = ref.get_sort();
      assert(sort.is_bv());
      const auto n = sort.bv_size();
      z3::context& ctx = ref.ctx();
      return z3::shl(ctx.bv_val(1, n), i);
    }
    
    z3::expr get_empty_set(z3::context& ctx, size_t n) const final {
      return ctx.bv_val(0, n);
    }
    
    z3::expr set_union(const z3::expr& a, const z3::expr& b) const final {
      return a | b;
    }
    
    z3::expr set_add(const z3::expr& set, size_t i) const final {
      return set_union(set, get_singleton(set, i));
    }
    
    z3::expr set_member(const z3::expr& set, size_t i) const final {
      z3::context& ctx = set.ctx();
      return set.extract(i, i) == ctx.bv_val(1, 1);
    }
  };
  
}
