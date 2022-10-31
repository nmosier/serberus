
#include <ctime>

#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <vector>
#include <variant>
#include <iomanip>

#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IR/IntrinsicsX86.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Clou/Clou.h>
#include <llvm/ADT/STLExtras.h>

#include "clou/MinCutSMT.h"
#include "clou/util.h"
#include "clou/Transmitter.h"
#include "clou/CommandLine.h"
#include "clou/Mitigation.h"
#include "clou/Log.h"
#include "clou/analysis/NonspeculativeTaintAnalysis.h"
#include "clou/analysis/SpeculativeTaintAnalysis.h"
#include "clou/analysis/LeakAnalysis.h"
#include "clou/Stat.h"
#include "clou/containers.h"

namespace clou {
  namespace {

    llvm::cl::opt<bool> log_times {
      "clou-times",
      llvm::cl::desc("Log execution times of Mitigate Pass"),
    };

    void print_debugloc(llvm::raw_ostream& os, const llvm::Value *V) {
      if (const auto *I = llvm::dyn_cast<llvm::Instruction>(V)) {
	if (const auto& DL = I->getDebugLoc()) {
	  DL.print(os);
	  return;
	}
      }
      os << "(none)";
    }

    std::string str_debugloc(const llvm::Value *V) {
      std::string s;
      llvm::raw_string_ostream ss(s);
      print_debugloc(ss, V);
      return s;
    }

    struct Node {
      llvm::Instruction *V;
      Node() {}
      Node(llvm::Instruction *V): V(V) {}
      bool operator<(const Node& o) const { return V < o.V; }
      bool operator==(const Node& o) const { return V == o.V; }
      bool operator!=(const Node& o) const { return V != o.V; }
    };

    llvm::raw_ostream& operator<<(llvm::raw_ostream& os, const Node& node) {
      return os << *node.V;
    }
    std::ostream& operator<<(std::ostream& os_, const Node& node) {
      llvm::raw_os_ostream os(os_);
      os << node;
      return os_;
    }

    struct MitigatePass final : public llvm::FunctionPass {
      static inline char ID = 0;
    
      MitigatePass() : llvm::FunctionPass(ID) {}
    
      using Alg = MinCutSMT_BV<Node, int>;

      void getAnalysisUsage(llvm::AnalysisUsage &AU) const override {
	AU.addRequired<NonspeculativeTaint>();
	AU.addRequired<SpeculativeTaint>();
	AU.addRequired<LeakAnalysis>();
      }

      static bool ignoreCall(const llvm::CallBase *C) {
	if (auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(C)) {
	  if (llvm::isa<MitigationInst, llvm::DbgInfoIntrinsic>(II) ||  II->isAssumeLikeIntrinsic()) {
	    return true;
	  } else {
	    switch (II->getIntrinsicID()) {
	    case llvm::Intrinsic::fshr:
	    case llvm::Intrinsic::fshl:
	    case llvm::Intrinsic::x86_aesni_aesenc:
	    case llvm::Intrinsic::x86_aesni_aeskeygenassist:
	    case llvm::Intrinsic::x86_aesni_aesenclast:
	    case llvm::Intrinsic::vector_reduce_and:
	    case llvm::Intrinsic::vector_reduce_or:
	    case llvm::Intrinsic::umax:
	    case llvm::Intrinsic::umin:
	    case llvm::Intrinsic::smax:
	    case llvm::Intrinsic::smin:
	    case llvm::Intrinsic::ctpop:
	    case llvm::Intrinsic::bswap:
	    case llvm::Intrinsic::x86_pclmulqdq:
	    case llvm::Intrinsic::x86_rdrand_32:
	    case llvm::Intrinsic::vastart:
	    case llvm::Intrinsic::vaend:
	    case llvm::Intrinsic::vector_reduce_add:
	    case llvm::Intrinsic::abs:
	    case llvm::Intrinsic::umul_with_overflow:
	    case llvm::Intrinsic::bitreverse:
	    case llvm::Intrinsic::cttz:
	    case llvm::Intrinsic::usub_sat:
	    case llvm::Intrinsic::fmuladd:
	      return true;

	    case llvm::Intrinsic::memset:
	    case llvm::Intrinsic::memcpy:
	    case llvm::Intrinsic::memmove:
	      return false;

	    default:
	      warn_unhandled_intrinsic(II);
	      return false;
	    }
	  }
	} else {
	  return false;
	}	
      }

      static unsigned compute_edge_weight(llvm::Instruction *I, const llvm::DominatorTree& DT, const llvm::LoopInfo& LI) {
	if (WeightGraph) {
	  float score = 1.;
	  score *= instruction_loop_nest_depth(I, LI) + 1;
	  score *= 1. / (instruction_dominator_depth(I, DT) + 1);
	  return score * 100;
	} else {
	  return 1;
	}
      }

      template <class OutputIt>
      OutputIt getPublicLoads(llvm::Function& F, OutputIt out) {
	LeakAnalysis& LA = getAnalysis<LeakAnalysis>();
	SpeculativeTaint& ST = getAnalysis<SpeculativeTaint>();
	for (auto& I : llvm::instructions(F)) {
	  if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(&I)) {
	    if (LA.mayLeak(LI) && (!ST.secret(LI) || util::isConstantAddress(LI->getPointerOperand()))) {
	      *out++ = LI;
	    }
	  }
	}
	return out;
      }

      template <class OutputIt>
      static OutputIt getSecretStores(llvm::Function& F, NonspeculativeTaint& NST, SpeculativeTaint& ST, OutputIt out) {
	for (auto& B : F) {
	  for (auto& I : B) {
	    if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I)) {
	      if (!util::isSpeculativeInbounds(SI)) {
		llvm::Value *V = SI->getValueOperand();
		if (NST.secret(V) || ST.secret(V)) {
		  *out++ = SI;
		}
	      }
	    }
	  }
	}
	return out;
      }

      template <class OutputIt>
      static OutputIt getCtrls(llvm::Function& F, OutputIt out) {
	for (auto& I : llvm::instructions(F)) {
	  if (llvm::CallBase *CB = llvm::dyn_cast<llvm::CallBase>(&I)) {
	    if (!ignoreCall(CB)) {
	      *out++ = CB;
	    }
	  } else if (llvm::isa<llvm::ReturnInst>(&I)) {
	    *out++ = &I;
	  }
	}
	return out;
      }

      static void getTransmitters(llvm::Function& F, SpeculativeTaint& ST, std::map<llvm::Instruction *, ISet>& out) {
	for (auto& I : llvm::instructions(F)) {
	  for (const TransmitterOperand& op : get_transmitter_sensitive_operands(&I)) {
	    if (ST.secret(op.V)) {
	      auto *op_I = llvm::cast<llvm::Instruction>(op.V);
	      out[&I].insert(op_I);
	    }
	  }
	}
      }

      template <class OutputIt>
      static void checkArgs(llvm::Function& F, OutputIt out) {
	llvm::DataLayout DL(F.getParent());
	unsigned counter = 0;
	for (llvm::Argument& A : F.args()) {
	  if (counter >= 6) {
	    *out++ = std::make_pair(&A, false);
	  } else {
	    llvm::Type *T = A.getType();
	    const auto bits = DL.getTypeSizeInBits(T);
	    constexpr auto regbits = 64;
	    counter += ((bits + regbits - 1) / regbits);
	    *out++ = std::make_pair(&A, counter <= 6);
	  }
	}
      }

      void staticStats(llvm::json::Object& j, llvm::Function& F) {
	// List callees
	auto& callees = j["callees"] = llvm::json::Array();
	auto& indirect_calls = j["indirect_calls"] = false;
	std::set<const llvm::Function *> seen;
	for (const llvm::CallBase& CB : util::instructions<llvm::CallBase>(F)) {
	  if (const llvm::Function *F = CB.getCalledFunction()) {
	    if (seen.insert(F).second) {
	      callees.getAsArray()->push_back(llvm::json::Value(F->getName()));
	    }
	  } else {
	    indirect_calls = true;
	  }
	}

	// Get all transmitters
	CountStat stat_naive_loads(j, "naive_loads");
	CountStat stat_naive_xmits(j, "naive_xmits");
	for (llvm::Instruction& I : llvm::instructions(F)) {
	  bool is_xmit = false;
	  for (const auto& op : get_transmitter_sensitive_operands(&I)) {
	    if (op.kind == TransmitterOperand::TRUE) {
	      for ([[maybe_unused]] const auto& V : get_incoming_loads(op.V)) {
		is_xmit = true;
		++stat_naive_loads;
	      }
	    }
	  }
	  if (is_xmit)
	    ++stat_naive_xmits;
	}
      }

      void saveLog(llvm::json::Object&& j, llvm::Function& F) {
	if (!ClouLog)
	  return;
	std::string s;
	llvm::raw_string_ostream ss(s);
	ss << ClouLogDir << "/" << F.getName() << ".json";
	std::ofstream os_cxx(s);
	llvm::raw_os_ostream os_llvm(os_cxx);
	os_llvm << llvm::json::Value(std::move(j));
      }
    
      bool runOnFunction(llvm::Function &F) override {
	if (whitelisted(F))
	  return false;
	
	clock_t t_start = clock();
	
	auto& NST = getAnalysis<NonspeculativeTaint>();
	auto& ST = getAnalysis<SpeculativeTaint>();
	auto& LA = getAnalysis<LeakAnalysis>();

	
	llvm::DominatorTree DT(F);
	llvm::LoopInfo LI(DT);

	// Set of speculatively public loads	
	std::set<llvm::LoadInst *> spec_pub_loads;
	getPublicLoads(F, std::inserter(spec_pub_loads, spec_pub_loads.end()));
	
	// Set of secret, speculatively out-of-bounds stores (speculative or nonspeculative)
	std::set<llvm::StoreInst *> oob_sec_stores;
	getSecretStores(F, NST, ST, std::inserter(oob_sec_stores, oob_sec_stores.end()));

	// Set of control-transfer instructions that require all previous OOB stores to have resolved
	ISet ctrls;
	getCtrls(F, std::inserter(ctrls, ctrls.end()));

	// Set of transmitters
	std::map<llvm::Instruction *, ISet> transmitters;
	getTransmitters(F, ST, transmitters);

	Alg A;
	Alg::Graph& G = A.G;

	/* Stats */
	llvm::json::Object log;

	CountStat stat_ncas_load(log, "sts_ncas_cal");
	CountStat stat_ncas_ctrl(log, "sts_ncas_ctrl");
	CountStat stat_st_udts(log, "sts_udt");
	CountStat stat_instructions(log, "instructions", std::distance(llvm::inst_begin(F), llvm::inst_end(F)));
	CountStat stat_nonspec_secrets(log, "maybe_nonspeculative_secrets",
				       llvm::count_if(llvm::instructions(F), [&] (auto& I) { return NST.secret(&I); }));
	CountStat stat_nonspec_publics(log, "definitely_nonspeculative_public",
				       llvm::count_if(util::nonvoid_instructions(F), [&] (auto& I) { return !NST.secret(&I); }));
	CountStat stat_spec_secrets(log, "maybe_speculative_secrets",
				    llvm::count_if(llvm::instructions(F), [&] (auto& I) { return ST.secret(&I); }));
	CountStat stat_spec_publics(log, "definitely_speculative_publics",
				    llvm::count_if(util::nonvoid_instructions(F), [&] (auto& I) { return !ST.secret(&I); }));
	CountStat stat_leaks(log, "maybe_leaked_instructions",
			     llvm::count_if(llvm::instructions(F), [&] (auto& I) { return LA.mayLeak(&I); }));
	CountStat stat_nonleaks(log, "definitely_not_leaked_instructions",
				llvm::count_if(util::nonvoid_instructions(F), [&] (auto& I) { return !LA.mayLeak(&I); }));
	CountStat stat_leaked_spec_secrets(log, "maybe_leaked_speculative_secret_loads",
					   llvm::count_if(util::instructions<llvm::LoadInst>(F), [&] (auto& I) {
					     return LA.mayLeak(&I) && ST.secret(&I);
					   }));


	{
	  // Transmitter stats
	  CountStat stat_xmit_pub(log, "xmit_safe");
	  CountStat stat_xmit_pseudo(log, "xmit_pseudo");
	  CountStat stat_xmit_true(log, "xmit_true");
	  for (auto& I : llvm::instructions(F)) {
	    enum Kind {
	      NONE = 0,
	      SAFE,
	      PSEUDO,
	      TRUE,
	    };
	    Kind kind = NONE;
	    for (const auto& op : get_transmitter_sensitive_operands(&I)) {
	      if (ST.secret(op.V)) {
		switch (op.kind) {
		case TransmitterOperand::TRUE:
		  kind = std::max(kind, TRUE);
		  break;
		case TransmitterOperand::PSEUDO:
		  kind = std::max(kind, PSEUDO);
		  break;
		default: std::abort();
		}
	      } else {
		kind = std::max(kind, SAFE);
	      }
	    }

	    switch (kind) {
	    case NONE: break;
	    case SAFE: ++stat_xmit_pub; break;
	    case PSEUDO: ++stat_xmit_pseudo; break;
	    case TRUE: ++stat_xmit_true; break;
	    default: std::abort();
	    }
	    
	  }
	}
	
	
	{
	  CountStat stat_cas(log, "cas");
	  CountStat stat_ncas_pub(log, "ncas_pub");
	  CountStat stat_ncas_sec(log, "ncas_sec");
	  
	  for (auto& SI : util::instructions<llvm::StoreInst>(F)) {
	    if (util::isConstantAddress(SI.getPointerOperand())) {
	      ++stat_cas;
	    } else {
	      auto *V = SI.getValueOperand();
	      if (NST.secret(V) || ST.secret(V)) {
		++stat_ncas_sec;
	      } else {
		++stat_ncas_pub;
	      }
	    }
	  }

	  CountStat stat_cal(log, "cal");
	  CountStat stat_ncal_leak(log, "ncal_leak");
	  CountStat stat_ncal_safe(log, "ncal_safe");
	  CountStat stat_cal_specsec(log, "cal_specsec");
	  CountStat stat_cal_leak(log, "cal_leak");

	  for (auto& LI : util::instructions<llvm::LoadInst>(F)) {
	    if (util::isConstantAddress(LI.getPointerOperand())) {
	      ++stat_cal;
	      if (LA.mayLeak(&LI)) {
		++stat_cal_leak;
	      }
	      if (ST.secret(&LI)) {
		++stat_cal_specsec;
	      }
	    } else {
	      assert(ST.secret(&LI));
	      if (LA.mayLeak(&LI)) {
		++stat_ncal_leak;
	      } else {
		++stat_ncal_safe;
	      }
	    }
	  }
	}

	
	if (enabled.oobs) {

	  // Create ST-pairs for {oob_sec_stores X spec_pub_loads}
	  CountStat stat_spec_pub_loads(log, "spec_pub_loads", spec_pub_loads.size());

	  for (auto *SI : oob_sec_stores) {
	    for (auto& LI : util::instructions<llvm::LoadInst>(F)) {
	      if (LA.mayLeak(&LI)) {
		if (ST.secret(&LI)) {
		  if (util::isConstantAddress(LI.getPointerOperand())) {
		    // Add pair from this load to all subsequent
		    for (const auto& [transmitter, transmit_ops] : transmitters) {
		      for (auto *op_I : transmit_ops) {
			if (ST.taints[op_I].contains(&LI)) {
			  A.add_st({.s = SI, .t = transmitter});
			  ++stat_ncas_load;
			}
		      }
		    }
		  } else {
		    // we're already mitigating this as UDT
		  }
		} else {
		  // Speculatively Public Constant-Address Store
		  A.add_st({.s = SI, .t = &LI});
		  ++stat_ncas_load;	  
		}
	      }
	    }
	  }


#if 0
	  for (auto *LI : spec_pub_loads) {
	    for (auto *SI : oob_sec_stores) {
	      A.add_st({.s = SI, .t = LI});
	      ++stat_ncas_load;
	    }
	  }
#endif

	  // Create ST-pairs for {oob_sec_stores X ctrls}
	  for (auto *ctrl : ctrls) {
	    for (auto *SI : oob_sec_stores) {
	      A.add_st({.s = SI, .t = ctrl});
	      ++stat_ncas_ctrl;
	    }
	  }
	
	}

	if (enabled.udt) {

	  // Create ST-pairs for {source X transmitter}
	  for (const auto& [transmitter, transmit_ops] : transmitters) {
	    for (auto *op_I : transmit_ops) {
	      for (const auto& [source, kind] : ST.taints.at(op_I)) {
		if (kind == SpeculativeTaint::ORIGIN) {
		  assert(llvm::isa<llvm::LoadInst>(source));
		  assert(!util::isConstantAddress(llvm::cast<llvm::LoadInst>(source)));
		  A.add_st({.s = source, .t = transmitter});
		  ++stat_st_udts;
		}
	      }
	    }
	  }
	  
	}
	
	// Add CFG to graph
	for (auto& B : F) {
	  for (auto& dst : B) {
	    for (auto *src : llvm::predecessors(&dst)) {
	      G[src][&dst] = compute_edge_weight(&dst, DT, LI);
	    }
	  }
	}


	/* Add edge connecting return instructions to entry instructions. 
	 * This allows us to capture aliases across invocations of the same function.
	 */
	for (auto& B : F) {
	  for (auto& I : B) {
	    if (llvm::isa<llvm::ReturnInst>(&I)) {
	      auto *entry = &F.getEntryBlock().front();
	      G[&I][entry] = compute_edge_weight(entry, DT, LI);
	    }
	  }
	}

	// Run algorithm to obtain min-cut
	const clock_t solve_start = clock();
	A.run();
	const clock_t solve_stop = clock();
	const float solve_duration = (static_cast<float>(solve_stop) - static_cast<float>(solve_start)) / CLOCKS_PER_SEC;
	auto& cut_edges = A.cut_edges;

	// Output DOT graph, color cut edges
	if (ClouLog) {
	  // emit dot
	  {
	    std::stringstream path;
	    path << ClouLogDir << "/" << F.getName().str() << ".dot";
	    std::ofstream f(path.str());
	    f << "digraph {\n";

	    // collect stores + sinks
	    std::map<Node, std::size_t> nodes;
	    for (llvm::BasicBlock& B : F) {
	      for (llvm::Instruction& I : B) {
		nodes.emplace(&I, nodes.size());
	      }
	    }

	    std::map<Node, std::string> special;
	    for (const Alg::ST& st : A.sts) {
	      special[st.t] = "blue"; // TODO: used to be green, but sometimes nodes are both sources and sinks.
	      special[st.s] = "blue";
	    }
	    
	    for (const auto& [node, i] : nodes) {
	      f << "node" << i << " [label=\"" << node << "\"";
	      if (special.contains(node)) {
		f << ", style=filled, fontcolor=white, fillcolor=" << special[node] << "";
	      }
	      f << "];\n";
	    }

	    // Add ST-pairs as dotted gray edges
	    for (const auto& st : A.sts) {
	      f << "node" << nodes.at(st.s) << " -> node" << nodes.at(st.t) << " [style=\"dashed\", color=\"blue\", penwidth=3]\n";
	    }
	  
            
	    for (const auto& [u, usucc] : G) {
	      for (const auto& [v, weight] : usucc) {
		if (weight > 0) {
		  f << "node" << nodes.at(u) << " -> " << "node" << nodes.at(v) << " [label=\"" << weight << "\", penwidth=3";
		  if (std::find(cut_edges.begin(),
				cut_edges.end(),
				Alg::Edge({.src = u, .dst = v})) != cut_edges.end()) {
		    f << ", color=\"red\"";
		  }
		  f << "];\n";
		}
	      }
	    }
            
	    f << "}\n";
	  }

	  // emit stats
	  {
	    log["function_name"] = F.getName();
	    log["solution_time"] = util::make_string_std(std::setprecision(3), solve_duration) + "s";
	    log["num_st_pairs"] = A.sts.size();
	    VSet sources, sinks;
	    for (const auto& st : A.sts) {
	      sources.insert(st.s.V);
	      sinks.insert(st.t.V);
	    }
	    log["distinct_sources"] = sources.size();
	    log["distinct_sinks"] = sinks.size();

	    /* Potential optimization:
	     * Try to reduce to product whereever possible.
	     */
	    {
	      VSetSet gsources, gsinks;
	      collapseOptimization(A.sts, gsources, gsinks);
	      log["source_groups"] = gsources.size();
	      log["sink_groups"] = gsinks.size();
	    }

	    auto& j_sts = log["st_pairs"] = llvm::json::Array();
	    for (const auto& st : A.sts) {
	      std::string source, sink;
	      llvm::raw_string_ostream source_os(source), sink_os(sink);
	      print_debugloc(source_os, st.s.V);
	      print_debugloc(sink_os, st.t.V);
	      j_sts.getAsArray()->push_back(llvm::json::Object({
		    {.K = "source", .V = source},
		    {.K = "sink", .V = sink},
		  }));
	    }

	    // TODO: emit this to separate file?
	    auto& j_cuts = log["cut_edges"] = llvm::json::Array();
	    for (const auto& cut : cut_edges) {
	      std::string src, dst;
	      llvm::raw_string_ostream src_os(src), dst_os(dst);
	      print_debugloc(src_os, cut.src.V);
	      print_debugloc(dst_os, cut.dst.V);
	      j_cuts.getAsArray()->push_back(llvm::json::Object({
		    {.K = "src", .V = src},
		    {.K = "dst", .V = dst},
		  }));
	    }

	    log["lfences"] = cut_edges.size(); 
	  }
	}

	// Mitigations
	auto& lfence_srclocs = log["lfence_srclocs"] = llvm::json::Array();
	for (const auto& [src, dst] : cut_edges) {
	  CreateMitigation(getMitigationPoint(src.V, dst.V), "clou-mitigate-pass");

	  // Print out mitigation info
	  if (ClouLog) {
	    lfence_srclocs.getAsArray()->push_back(llvm::json::Object({
		  {.K = "src", .V = str_debugloc(src.V)},
		  {.K = "dst", .V = str_debugloc(dst.V)},
		}));
	  }
	}

#if 0
	// Mark all public values as nospill
	for (auto& B : F) {
	  for (auto& I : B) {
	    if (!(ST.secret(&I) || I.getType()->isVoidTy())) {
	      I.setMetadata("clou.nospill", llvm::MDNode::get(F.getContext(), {}));
	    }
	  }
	}
#endif

	
	
	const clock_t t_stop = clock();
	if (log_times) {
	  trace("time %.3f %s", static_cast<float>(t_stop - t_start) / CLOCKS_PER_SEC, F.getName().str().c_str());
	}

	staticStats(log, F);
	saveLog(std::move(log), F);
	
        return true;
      }

      static bool shouldCutEdge(llvm::Instruction *src, llvm::Instruction *dst) {
	return llvm::predecessors(dst).size() > 1 || llvm::successors_inst(src).size() > 1;
      }

      static llvm::Instruction *getMitigationPoint(llvm::Instruction *src, llvm::Instruction *dst) {
	if (shouldCutEdge(src, dst)) {
	  assert(src->isTerminator() && dst == &dst->getParent()->front());
	  llvm::BasicBlock *B = llvm::SplitEdge(src->getParent(), dst->getParent());
	  return &B->front();
	} else {
	  return dst;
	}
      }

      template <class STs>
      static void collapseOptimization(const STs& sts, VSetSet& src_out, VSetSet& sink_out) {
	VSet src_in, sink_in;
	for (const auto& st : sts) {
	  src_in.insert(st.s.V);
	  sink_in.insert(st.t.V);
	}
  
	VMap s_ts, t_ss;
	for (const auto& st : sts) {
	  s_ts[st.s.V].insert(st.t.V);
	  t_ss[st.t.V].insert(st.s.V);
	}

	// Flip maps
	std::map<std::set<llvm::Value *>, std::set<llvm::Value *>> ts_to_ss, ss_to_ts;
	for (const auto& [s, ts] : s_ts) {
	  ts_to_ss[ts].insert(s);
	}
	for (const auto& [t, ss] : t_ss) {
	  ss_to_ts[ss].insert(t);
	}

	// Copy out sets
	for (const auto& [_, ss] : ts_to_ss) {
	  src_out.insert(ss);
	}
	for (const auto& [_, ts] : ss_to_ts) {
	  sink_out.insert(ts);
	}

	// Validate results
	{
	  VSet src_out_flat, sink_out_flat;
	  for (const auto& gsrc : src_out) {
	    src_out_flat.insert(gsrc.begin(), gsrc.end());
	  }
	  for (const auto& gsink : sink_out) {
	    sink_out_flat.insert(gsink.begin(), gsink.end());
	  }
#if 0
	  if (src_out_flat != src_in || sink_out_flat != sink_in) {
	    auto& os = llvm::errs();
	    os << "sts:\n";
	    for (const auto& st : sts) {
	      os << "  (" << st.s.V << ", " << st.t.V << ")\n";
	    }
	    os << "src_in: " << src_in << "\n";
	    os << "sink_in: " << sink_in << "\n";
	    os << "s_ts: " << s_ts << "\n";
	    os << "t_ss: " << t_ss << "\n";
	    os << "src_out: " << src_out << "\n";
	    os << "sink_out: " << sink_out << "\n";
	    
	  }
#endif
	  assert(src_out_flat == src_in);
	  assert(sink_out_flat == sink_in);
	}
      }
    };

    llvm::RegisterPass<MitigatePass> X{"clou-mitigate",
					   "Clou Mitigation Pass"};
    util::RegisterClangPass<MitigatePass> Y;
  }
}

