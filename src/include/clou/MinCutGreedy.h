#pragma once

#include "MinCutBase.h"
#include "clou/FordFulkerson.h"

#include <queue>
#include <stack>
#include <unordered_map>
#include <map>
#include <set>

#include <llvm/ADT/SmallSet.h>
#include <llvm/Clou/Clou.h>
#include <llvm/ADT/STLExtras.h>

#define DEBUG(...) ;

#define CHECK_CUTS 0

namespace clou {

  template <class Node>
  class MinCutGreedy final : public MinCutBase<Node, unsigned> {
  public:
    using Weight = unsigned;
    using Super = MinCutBase<Node, Weight>;
    using Edge = typename Super::Edge;
    using ST = typename Super::ST;

  private:
    using Idx = unsigned;
    struct IdxST {
      std::vector<std::set<Idx>> waypoints;
      template <typename... Ts> IdxST(Ts&&... args): waypoints(std::forward<Ts>(args)...) {}
      auto operator<=>(const IdxST&) const = default;
    };
    struct IdxEdge {
      Idx src, dst;
      auto operator<=>(const IdxEdge& o) const = default;
    };    
    using IdxGraph = std::vector<std::map<Idx, Weight>>;

    /* This optimization removes all the unreachable nodes in an s-t list.
     * Requires a follow-up pass to remove s-t lists containing an empty set.
     */
    bool optimize_sts_cull_unreachables(ST& st_) const {
      auto& G = this->G;
      size_t removed = 0;
      auto& st = st_.waypoints;
      for (auto S_it = st.begin(), T_it = std::next(S_it); T_it != st.end(); ++S_it, ++T_it) {
	const auto& S = *S_it;
	auto& T = *T_it;

	// Compute reach of S.
	std::stack<Node> todo;
	for (const Node& s : S)
	  todo.push(s);
	std::set<Node> reach;
	while (!todo.empty()) {
	  const Node u = todo.top();
	  todo.pop();
	  for (const auto& [v, w] : G[u])
	    if (reach.insert(v).second)
	      todo.push(v);
	}

	// Remove any t's that aren't reached.
	removed += std::erase_if(T, [&reach] (const Node& v) {
	  return !reach.contains(v);
	});
      }
      return removed > 0;
    }

    bool optimize_sts_cull_sources(ST& st_) const {
      auto& G = this->G;
      bool changed = false;
      auto& st = st_.waypoints;
      for (auto S_it = st.begin(), T_it = std::next(S_it); T_it != st.end(); ++S_it, ++T_it) {
	auto& S = *S_it;
	const auto& T = *T_it;

	// Compute reach of each s \in S.
	for (auto s_it = S.begin(); s_it != S.end(); ) {
	  std::stack<Node> todo;
	  todo.push(*s_it);
	  std::set<Node> reach;
	  while (!todo.empty()) {
	    const Node u = todo.top();
	    todo.pop();
	    for (const auto& [v, w] : G[u]) {
	      if (S.contains(v) && !T.contains(v))
		continue;
	      if (reach.insert(v).second)
		todo.push(v);
	    }
	  }

	  // If no t \in T is reached, then we can safely remove the source.
	  const bool reached_T = llvm::any_of(T, [&reach] (const Node& t) {
	    return reach.contains(t);
	  });
	  if (reached_T) {
	    ++s_it;
	  } else {
	    s_it = S.erase(s_it);
	    changed = true;
	  }
	}
      }

      return changed;
    }


    // TODO: optimize_sts_cull_sinks -- similar to *_soures

    bool optimize_sts_remove_emptyset(std::vector<ST>& sts) const {
      const auto in_size = sts.size();
      for (auto it = sts.begin(); it != sts.end(); ) {
	const bool has_emptyset = llvm::any_of(it->waypoints, [] (const auto& s) {
	  return s.empty();
	});
	if (has_emptyset)
	  it = sts.erase(it);
	else
	  ++it;
      }
      const auto out_size = sts.size();
      return in_size != out_size;
    }

    

    bool optimize_sts_remove_redundant_internal_st(ST& st_) const {
      auto& st = st_.waypoints;
      auto& G = this->G;

      bool changed = false;

      // Check if there's a path from A to C without hitting B.
      auto B_it = std::next(st.begin());
      while (std::distance(B_it, st.end()) > 1) {
	const auto A_it = std::prev(B_it);
	const auto C_it = std::next(B_it);

	// Check if there's a path from A to C without hitting B.
	std::stack<Node> todo;
	for (const Node& a : *A_it)
	  todo.push(a);
	std::set<Node> reach;
	while (!todo.empty()) {
	  const Node u = todo.top();
	  todo.pop();
	  for (const auto& [v, w] : G[u])
	    if (!B_it->contains(v) && reach.insert(v).second)
	      todo.push(v);
	}

	// If no c \in C is reached, then there exists no path directly from A to C. Therefore we can remove B entirely.
	const bool no_AC_path = llvm::none_of(*C_it, [&] (const Node& v) {
	  return reach.contains(v);
	});
	if (no_AC_path) {
	  B_it = st.erase(B_it);
	  changed = true;
	} else {
	  ++B_it;
	}
      }

      return changed;
    }

    bool optimize_sts_remove_duplicates(std::vector<ST>& sts) const {
      const auto in_size = sts.size();
      llvm::sort(sts);
      sts.resize(std::unique(sts.begin(), sts.end()) - sts.begin());
      const auto out_size = sts.size();
      return in_size != out_size;
    }

    bool optimize_st_nop(ST& st) const { return false; }
    bool optimize_sts_nop(std::vector<ST>& sts) const { return false; }

    void optimize_sts(const std::vector<ST>& in_sts, std::vector<ST>& out_sts) const {
      out_sts = in_sts;

      typedef bool (MinCutGreedy::*optimize_st_t)(ST&) const;
      typedef bool (MinCutGreedy::*optimize_sts_t)(std::vector<ST>&) const;
      
      optimize_st_t local_opts[] = {
	&MinCutGreedy::optimize_st_nop,
	&MinCutGreedy::optimize_sts_cull_unreachables,
	&MinCutGreedy::optimize_sts_cull_sources,
	// &MinCutGreedy::optimize_sts_remove_redundant_internal_st
      };

      optimize_sts_t global_opts[] = {
	&MinCutGreedy::optimize_sts_nop,
	&MinCutGreedy::optimize_sts_remove_emptyset,
	&MinCutGreedy::optimize_sts_remove_duplicates,
      };

      const auto compute_size = [&] (const std::vector<ST>& sts) -> size_t {
	size_t count = 0;
	for (const ST& st : sts)
	  for (const auto& p : st.waypoints)
	    count += p.size();
	return count;
      };

      const size_t in_size = compute_size(out_sts);
      
      bool changed;
      do {
	changed = false;
	for (optimize_st_t local_opt : local_opts)
	  for (ST& st : out_sts)
	    changed |= (this->*local_opt)(st);
	for (optimize_sts_t global_opt : global_opts)
	  changed |= (this->*global_opt)(out_sts);
      } while (changed);

      // Expand them [DEBUG]
      if (false) {
	out_sts.clear();
	for (const ST& oldst : in_sts) {
	  if (llvm::any_of(oldst.waypoints, [] (const auto& s) { return s.empty(); }))
	    continue;
	  using iterator = std::set<Node>::iterator;
	  std::vector<iterator> stack;
	  for (const auto& s : oldst.waypoints)
	    stack.push_back(s.begin());
	  unsigned count = 0;
	  while (true) {
	    ST& newst = out_sts.emplace_back();
	    for (iterator it : stack)
	      newst.waypoints.push_back(std::set<Node>{*it});
	    ++count;
	    unsigned i = 0;
	    while (i < stack.size()) {
	      auto& it = stack[i];
	      assert(it != oldst.waypoints[i].end());
	      ++it;
	      if (it == oldst.waypoints[i].end()) {
		it = oldst.waypoints[i].begin();
		++i;
	      } else {
		break;
	      }
	    }
	    if (i == stack.size())
	      break;
	  }
	  unsigned sizes = 1;
	  for (const auto& s : oldst.waypoints)
	    sizes *= s.size();
	  assert(count == sizes);
	}
      }
      
      const size_t out_size = compute_size(out_sts);
      
      llvm::errs() << "reduced sts: " << in_size << " to " << out_size << "\n";
    }
    
  public:

    void run() override {
#if 0
      // sort and de-duplicate sts
      {
	const std::set<ST> st_set(this->sts.begin(), this->sts.end());
	this->sts.resize(st_set.size());
	llvm::copy(st_set, this->sts.begin());
      }
#else
      {
	std::vector<ST> opt_sts;
	optimize_sts(this->sts, opt_sts);
	this->sts = std::move(opt_sts);
      }
#endif

      llvm::errs() << "min-cut on " << getNodes().size() << " nodes\n";

      /* New algorithm:
       * Maintain a set of LFENCE insertion points.
       * For each ST pair:
       *   Perform the optimal min cut.
       */

      // Convert everything into efficient representation.
      using Idx = unsigned;

      // Get nodes.
      std::vector<Node> nodes;
      llvm::copy(getNodes(), std::back_inserter(nodes));
      const auto node_to_idx = [&nodes] (const Node& node) -> Idx {
	const auto it = llvm::lower_bound(nodes, node);
	assert(it != nodes.end() && *it == node);
	return it - nodes.begin();
      };
      const auto idx_to_node = [&nodes] (Idx idx) -> const Node& {
	assert(idx < nodes.size());
	return nodes[idx];
      };

      // Get index graph.
      IdxGraph G(nodes.size());
      for (const auto& [src, dsts] : this->G) {
	const Idx isrc = node_to_idx(src);
	auto& idsts = G[isrc];
	for (const auto& [dst, w] : dsts) {
	  const Idx idst = node_to_idx(dst);
	  idsts[idst] = w;
	}
      }

      // Get index sts.
      std::vector<IdxST> sts;
      for (const ST& st : this->sts) {
	auto& ist = sts.emplace_back();
	for (const auto& way : st.waypoints) {
	  auto& iway = ist.waypoints.emplace_back();
	  llvm::transform(way, std::inserter(iway, iway.end()), node_to_idx);
	}
      }

      bool changed;
      enum class Mode {Replace, Augment} mode = Mode::Replace;
      using Cuts = std::vector<std::vector<IdxEdge>>;
      using CutsHistory = std::set<Cuts>;
      Cuts cuts(sts.size());
      CutsHistory cuts_hist;
      const IdxGraph OrigG = G; // mainly for checking weights
      int num_iterations = 0;
      do {
	llvm::errs() << "\rnum_iterations: " << ++num_iterations;
	
	changed = false;

	for (const auto& [st, cut] : llvm::zip(sts, cuts)) {
	  
	  // Add old cut edges back in to graph.
	  const auto oldcut = std::move(cut);
	  if (mode == Mode::Replace) {
	    for (const IdxEdge& e : oldcut) {
	      const Weight w = OrigG[e.src].at(e.dst);
	      [[maybe_unused]] const bool inserted = G[e.src].insert(std::make_pair(e.dst, w)).second;
	      assert(inserted && "Cut edge still in graph G!");
	    }
	  }

	  // Compute new local min cut.
	  const auto newcut_tmp = ford_fulkerson_multi(G, st.waypoints);
	  std::vector<IdxEdge> newcut(newcut_tmp.size());
	  llvm::transform(newcut_tmp, newcut.begin(), [] (const auto& p) -> IdxEdge {
	    return {.src = p.first, .dst = p.second};
	  });
	  if (mode == Mode::Augment)
	    llvm::copy(oldcut, std::back_inserter(newcut));
	  llvm::sort(newcut);
	  std::unique(newcut.begin(), newcut.end());
	  
#if CHECK_CUTS
	  {
	    std::set<IdxEdge> cutset;
	    llvm::copy(newcut, std::inserter(cutset, cutset.end()));
	    checkCutST(st.waypoints, cutset, G);
	  }
#endif

	  // Remove new cut edges from G.
	  for (const IdxEdge& e : newcut) {
	    if (mode == Mode::Replace)
	      assert(G.at(e.src).find(e.dst) != G.at(e.src).end());
	    [[maybe_unused]] const auto erased = G[e.src].erase(e.dst);
	    if (mode == Mode::Replace)
	      assert(erased > 0);
	  }

	  // Check if changed.
	  if (oldcut != newcut)
	    changed = true;

	  // Update cut.
	  cut = std::move(newcut);

#if CHECK_CUTS
	  {
	    std::set<IdxEdge> cutset;
	    for (const auto& cutvec : cuts)
	      llvm::copy(cutvec, std::inserter(cutset, cutset.end()));
	    checkCutST(st.waypoints, cutset, OrigG);
	  }
#endif	  
	}

	if (changed) {
	  if (!cuts_hist.insert(cuts).second) {
	    assert(mode == Mode::Replace);
	    llvm::WithColor::warning() << "detected loop in min-cut algorithm\n";
	    mode = Mode::Augment;
	  }
	}

	// Double-check there're no redundant edges, since this should never happen.
#ifndef NDEBUG
	{
	  std::vector<IdxEdge> cutset;
	  for (const auto& cutvec : cuts)
	    llvm::copy(cutvec, std::back_inserter(cutset));
	  llvm::sort(cutset);
	  const auto orig_size = cutset.size();
	  std::unique(cutset.begin(), cutset.end());
	  const auto uniq_size = cutset.size();
	  assert(orig_size == uniq_size);
	}
#endif
	
      } while (changed);
      llvm::errs() << "\n";


#if CHECK_CUTS
      checkCut(cuts, sts, OrigG);
#endif

      // Now add all cut edges to master copy.
      for (const auto& cut : cuts)
	for (const IdxEdge& e : cut)
	  this->cut_edges.push_back({.src = idx_to_node(e.src), .dst = idx_to_node(e.dst)});
    }
    
  private:
    using EdgeSet = std::set<Edge>;

    static void checkCutST(llvm::ArrayRef<std::set<unsigned>> st, const std::set<IdxEdge>& cut, const IdxGraph& G) {
      assert(st.size() >= 2);
      const auto succs = [&] (unsigned u) {
	std::set<unsigned> succs;
	for (const auto& [v, w] : G.at(u)) {
	  assert(w > 0);
	  const IdxEdge e = {.src = u, .dst = v};
	  if (!cut.contains(e))
	    succs.insert(v);
	}
	return succs;
      };

      std::vector<std::set<unsigned>> particles;
      std::vector<std::map<unsigned, unsigned>> parents;
      std::set<unsigned> S = st.front();
      for (const std::set<unsigned>& T : st.drop_front()) {
	auto& parent = parents.emplace_back();
	particles.push_back(S);

	// Find all nodes reachable from S.
	std::set<unsigned> reach;
	std::stack<unsigned> todo;
	for (unsigned u : S)
	  todo.push(u);

	while (!todo.empty()) {
	  const unsigned u = todo.top();
	  todo.pop();
	  for (unsigned v : succs(u)) {
	    if (reach.insert(v).second) {
	      todo.push(v);
	      parent[v] = u;
	    }
	  }
	}

	S.clear();
	std::set_intersection(T.begin(), T.end(), reach.begin(), reach.end(), std::inserter(S, S.end()));
      }
      particles.push_back(S);
      
      if (!S.empty()) {
	llvm::WithColor::error() << "found s-t path in MinCutGreedy\n";
	std::abort();
      }
    }

    static void checkCut(llvm::ArrayRef<std::vector<IdxEdge>> cut, llvm::ArrayRef<IdxST> sts, const IdxGraph& G) {
      std::set<IdxEdge> cutset;
      for (const auto& cutvec : cut)
	llvm::copy(cutvec, std::inserter(cutset, cutset.end()));
      for (const IdxST& st : sts)
	checkCutST(st.waypoints, cutset, G);
    }

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

    mutable std::set<ST> disconnected_sts;
    mutable std::set<Node> disconnected_nodes;
    std::map<Edge, unsigned> computeReaching() {
      disconnected_nodes.clear();
      std::vector<Node> nodevec;
      llvm::copy(getNodes(), std::back_inserter(nodevec));
      llvm::erase_if(nodevec, [&] (const Node& u) { return disconnected_nodes.contains(u); });
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
	if (disconnected_sts.contains(st))
	  continue;
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
	if (disconnected_nodes.contains(src))
	  continue;
	const auto src_idx = node_to_idx(src);
	for (const auto& [dst, _] : dsts) {
	  if (disconnected_nodes.contains(dst))
	    continue;
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

      llvm::BitVector connected_nodes_bv;
      for ([[maybe_unused]] int i = 0; const auto& st : sts) {
	DEBUG(llvm::errs() << "\r\t\tsts processed: " << i++ << "/" << sts.size());
	const auto& fwd = s_reach.at(st.s);
	const auto& bwd = t_reach.at(st.t);
	llvm::BitVector both = fwd;
	both &= bwd;
	if (both.count() < 2) {
	  disconnected_sts.insert({.s = idx_to_node(st.s), .t = idx_to_node(st.t)});
	  continue;
	}
	connected_nodes_bv |= both;
	for (auto src_idx : both.set_bits()) {
	  for (auto dst_idx : G[src_idx]) {
	    if (both.test(dst_idx)) {
	      auto& result = results[src_idx][dst_idx];
	      ++result;
	    }
	  }
	}
      }

      llvm::BitVector disconnected_nodes_bv = connected_nodes_bv; disconnected_nodes_bv.flip();
      for (unsigned disconnected_node_idx : disconnected_nodes_bv.set_bits())
	disconnected_nodes.insert(idx_to_node(disconnected_node_idx));

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
