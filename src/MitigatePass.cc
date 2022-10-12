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

using VSet = std::set<llvm::Value *>;
using VSetSet = std::set<VSet>;
using VMap = std::map<llvm::Value *, VSet>;

namespace clou {
  namespace {

    llvm::cl::opt<bool> log_times {
      "clou-times",
      llvm::cl::desc("Log execution times of Mitigate Pass"),
    };

    using ISet = std::set<llvm::Instruction *>;
    using VSet = std::set<llvm::Value *>;
    using IMap = std::map<llvm::Instruction *, ISet>;

    void print_debugloc(llvm::raw_ostream& os, const llvm::Value *V) {
      if (const auto *I = llvm::dyn_cast<llvm::Instruction>(V)) {
	if (const auto& DL = I->getDebugLoc()) {
	  DL.print(os);
	  return;
	}
      }
      os << "(none)";
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
        float score = 1.;
        score *= instruction_loop_nest_depth(I, LI) + 1;
        score *= 1. / (instruction_dominator_depth(I, DT) + 1);
        return score * 100;
      }

      template <class OutputIt>
      OutputIt getPublicLoads(llvm::Function& F, OutputIt out) {
	NonspeculativeTaint& NST = getAnalysis<NonspeculativeTaint>();
	LeakAnalysis& LA = getAnalysis<LeakAnalysis>();
	SpeculativeTaint& ST = getAnalysis<SpeculativeTaint>();
	for (auto& I : llvm::instructions(F)) {
	  if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(&I)) {
	    if (!NST.secret(LI) && !ST.secret(LI) && LA.mayLeak(LI)) {
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
	std::ofstream log_cxx;
	if (ClouLog) {
	  std::string s;
	  llvm::raw_string_ostream ss(s);
	  ss << ClouLogDir << "/" << F.getName() << ".txt";
	  log_cxx.open(s);
	}
	llvm::raw_os_ostream log_llvm(log_cxx);

	CountStat stat_ncas_load("st_ncas_load", log_llvm);
	CountStat stat_ncas_ctrl("st_ncas_ctrl", log_llvm);
	CountStat stat_st_udts("st_udt", log_llvm);
	CountStat stat_instructions("instructions", log_llvm, std::distance(llvm::inst_begin(F), llvm::inst_end(F)));
	CountStat stat_nonspec_secrets("maybe_nonspeculative_secrets", log_llvm, llvm::count_if(llvm::instructions(F), [&] (auto& I) { return NST.secret(&I); }));
	CountStat stat_nonspec_publics("definitely_nonspeculative_public", log_llvm, llvm::count_if(util::nonvoid_instructions(F), [&] (auto& I) { return !NST.secret(&I); }));
	CountStat stat_spec_secrets("maybe_speculative_secrets", log_llvm, llvm::count_if(llvm::instructions(F), [&] (auto& I) { return ST.secret(&I); }));
	CountStat stat_spec_publics("definitely_speculative_publics", log_llvm, llvm::count_if(util::nonvoid_instructions(F), [&] (auto& I) { return !ST.secret(&I); }));
	CountStat stat_leaks("maybe_leaked_instructions", log_llvm, llvm::count_if(llvm::instructions(F), [&] (auto& I) { return LA.mayLeak(&I); }));
	CountStat stat_nonleaks("definitely_not_leaked_instructions", log_llvm, llvm::count_if(util::nonvoid_instructions(F), [&] (auto& I) { return !LA.mayLeak(&I); }));
	CountStat stat_leaked_spec_secrets("maybe_leaked_speculative_secret_loads", log_llvm, llvm::count_if(util::instructions<llvm::LoadInst>(F), [&] (auto& I) {
	  return LA.mayLeak(&I) && ST.secret(&I);
	}));
	CountStat stat_nca_sec_stores("nca_secret_stores", log_llvm, llvm::count_if(util::instructions<llvm::StoreInst>(F), [&] (auto& SI) {
	  auto *V = SI.getValueOperand();
	  return !util::isSpeculativeInbounds(&SI) && (NST.secret(V) || ST.secret(V));
	}));

	if (enabled.oobs) {

	  // Create ST-pairs for {oob_sec_stores X spec_pub_loads}
	  for (auto *LI : spec_pub_loads) {
	    for (auto *SI : oob_sec_stores) {
	      A.add_st({.s = SI, .t = LI});
	      ++stat_ncas_load;
	    }
	  }

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
	      for (auto *source : ST.taints.at(op_I)) {
		A.add_st({.s = source, .t = transmitter});
		++stat_st_udts;
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
	    log_llvm << "function_name: " << F.getName().str() << "\n";
	    log_llvm << "solution_time: " << util::make_string_std(std::setprecision(3), solve_duration) << "s\n";
	    log_llvm << "num_sts: " << A.sts.size() << "\n";
	    VSet sources, sinks;
	    for (const auto& st : A.sts) {
	      sources.insert(st.s.V);
	      sinks.insert(st.t.V);
	    }
	    log_llvm << "num_distinct_sources: " << sources.size() << "\n";
	    log_llvm << "num_distinct_sinks: " << sinks.size() << "\n";

	    /* Potential optimization:
	     * Try to reduce to product whereever possible.
	     */
	    {
	      VSetSet gsources, gsinks;
	      collapseOptimization(A.sts, gsources, gsinks);
	      log_llvm << "num_source_groups: " << sources.size() << " " << gsources.size() << "\n";
	      log_llvm << "num_sink_groups: " << sinks.size() << " " << gsinks.size() << "\n";
	    }

	    // TODO: Should emit this to separatae file.
	    for (const auto& st : A.sts) {
	      log_llvm << "st_pair: ";
	      print_debugloc(log_llvm, st.s.V);
	      log_llvm << " ";
	      print_debugloc(log_llvm, st.t.V);
	      log_llvm << "\n";
	    }

	    // TODO: emit this to separate file?
	    for (const auto& cut : cut_edges) {
	      log_llvm << "cut_edge: ";
	      print_debugloc(log_llvm, cut.src.V);
	      log_llvm << " ";
	      print_debugloc(log_llvm, cut.dst.V);
	      log_llvm << "\n";
	    }

	    log_llvm << "lfences: " << cut_edges.size() << "\n";
	  }
	}

	// Mitigations
	for (const auto& [src, dst] : cut_edges) {
	  CreateMitigation(getMitigationPoint(src.V, dst.V), "clou-mitigate-pass");
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

