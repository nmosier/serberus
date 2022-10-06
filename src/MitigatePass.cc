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

#include "clou/MinCutSMT.h"
#include "clou/util.h"
#include "clou/Transmitter.h"
#include "clou/CommandLine.h"
#include "clou/Mitigation.h"
#include "clou/Log.h"
#include "clou/analysis/NonspeculativeTaintAnalysis.h"
#include "clou/analysis/SpeculativeTaintAnalysis.h"
#include "clou/analysis/StackInitAnalysis.h"

using VSet = std::set<llvm::Value *>;
using VSetSet = std::set<VSet>;
using VMap = std::map<llvm::Value *, VSet>;

#if 0
namespace llvm {
  llvm::raw_ostream& operator<<(llvm::raw_ostream& os, const llvm::Value *V) {
    const std::string s = V->getNameOrAsOperand();
    if (s == "<badref>") {
      os << *V;
    } else {
      os << s;
    }
    return os;
  }
  
llvm::raw_ostream& operator<<(llvm::raw_ostream& os, const VSet& Vs) {
  os << "{";
  for (auto it = Vs.begin(); it != Vs.end(); ++it) {
    if (it != Vs.begin())
      os << ", ";
    os << *it;
  }
  os << "}";
  return os;
}

llvm::raw_ostream& operator<<(llvm::raw_ostream& os, const VSetSet& VSS) {
  os << "{\n";
  for (const auto& VS : VSS)
    os << "  " << VS << "\n";
  os << "}";
  return os;
}

llvm::raw_ostream& operator<<(llvm::raw_ostream& os, const VMap& VM) {
  os << "{\n";
  for (const auto& [V, VS] : VM) {
    os << "  " << V << " --> " << VS << "\n";
  }
  os << "}";
}
}
#endif

namespace clou {
  namespace {

    llvm::cl::opt<bool> log_times {
      "clou-times",
      llvm::cl::desc("Log execution times of Mitigate Pass"),
    };

    using ISet = std::set<llvm::Instruction *>;
    using VSet = std::set<llvm::Value *>;
    using IMap = std::map<llvm::Instruction *, ISet>;

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
	if (stack_mitigation_mode == StackMitigationMode::Lfence && false) {
	  llvm::errs() << "AAGH!\n";
	  abort();
	  AU.addRequired<StackInitAnalysis>();
	}
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
      static OutputIt getPublicLoads(llvm::Function& F, SpeculativeTaint& ST, OutputIt out) {
	for (auto& B : F) {
	  for (auto& I : B) {
	    if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(&I)) {
	      if (!ST.secret(LI)) {
		*out++ = LI;
	      }
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
	for (auto& B : F) {
	  for (auto& I : B) {
	    if (llvm::CallBase *CB = llvm::dyn_cast<llvm::CallBase>(&I)) {
	      if (!ignoreCall(CB)) {
		*out++ = CB;
	      }
	    } else if (llvm::isa<llvm::ReturnInst>(&I)) {
	      *out++ = &I;
	    }
	  }
	}
	return out;
      }

      static void getTransmitters(llvm::Function& F, SpeculativeTaint& ST, std::map<llvm::Instruction *, ISet>& out) {
	for (auto& B : F) {
	  for (auto& I : B) {
	    for (const TransmitterOperand& op : get_transmitter_sensitive_operands(&I)) {
	      if (ST.secret(op.V)) {
		auto *op_I = llvm::cast<llvm::Instruction>(op.V);
		out[&I].insert(op_I);
	      }
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
	clock_t t_start = clock();
	
	auto& NST = getAnalysis<NonspeculativeTaint>();
	auto& ST = getAnalysis<SpeculativeTaint>();
	StackInitAnalysis::Results *AIA;
	if (stack_mitigation_mode == StackMitigationMode::Lfence && false) {
	  AIA = &getAnalysis<StackInitAnalysis>().results;
	}
	
	llvm::DominatorTree DT(F);
	llvm::LoopInfo LI(DT);

	// Set of speculatively public loads	
	std::set<llvm::LoadInst *> spec_pub_loads;
	getPublicLoads(F, ST, std::inserter(spec_pub_loads, spec_pub_loads.end()));
	
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

	// Create ST-pairs for {oob_sec_stores X spec_pub_loads}
	for (auto *LI : spec_pub_loads) {
	  for (auto *SI : oob_sec_stores) {
	    A.add_st({.s = SI, .t = LI});
	  }
	}

	// Create ST-pairs for {oob_sec_stores X ctrls}
	for (auto *ctrl : ctrls) {
	  for (auto *SI : oob_sec_stores) {
	    A.add_st({.s = SI, .t = ctrl});
	  }
	}

	// Create ST-pairs for {source X transmitter}
	for (const auto& [transmitter, transmit_ops] : transmitters) {
	  for (auto *op_I : transmit_ops) {
	    for (auto *source : ST.taints.at(op_I)) {
	      A.add_st({.s = source, .t = transmitter});
	    }
	  }
	}

	if (stack_mitigation_mode == StackMitigationMode::Lfence && false) {
	  // EXPERIMENTAL: Create ST-pairs for {entry, return}.
	  for (auto& B : F) {
	    for (auto& I : B) {
	      if (llvm::isa<llvm::ReturnInst>(&I)) {
		A.add_st({.s = &F.getEntryBlock().front(), .t = &I});
	      }
	    }
	  }

	  // EXPERIMENTAL: Create ST-pairs for {arg-def, arg-use}
	  std::vector<std::pair<llvm::Argument *, bool>> args_regs;
	  for (auto& [arg, isreg] : args_regs) {
	    if (!isreg) {
	      for (llvm::Use& use : arg->uses()) {
		llvm::Instruction *user = llvm::cast<llvm::Instruction>(use.getUser());
		if (auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(user)) {
		  if (II->isAssumeLikeIntrinsic() || llvm::isa<llvm::DbgInfoIntrinsic>(II)) {
		    continue;
		  }
		}
		A.add_st({.s = &F.getEntryBlock().front(), .t = user});
	      }
	    }
	  }

	  // SUPER EXPERIMENTAL: Create ST-pairs for {alloca-first-init, alloca-first-use}
	  for (const auto& [alloca, result] : *AIA) {
	    for (auto *store : result.stores) {
	      for (auto *load : result.loads) {
		A.add_st({.s = store, .t = load});
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
	    std::stringstream ss;
	    ss << ClouLogDir << "/" << F.getName().str() << ".txt";
	    std::ofstream ofs(ss.str());
	    llvm::raw_os_ostream os(ofs);
	    os << "function_name: " << F.getName() << "\n";
	    ofs << "solution_time: " << std::setprecision(3) << solve_duration << "s\n";
	    ofs << "num_sts: " << A.sts.size() << "\n";
	    VSet sources, sinks;
	    for (const auto& st : A.sts) {
	      sources.insert(st.s.V);
	      sinks.insert(st.t.V);
	    }
	    ofs << "num_distinct_sources: " << sources.size() << "\n";
	    ofs << "num_distinct_sinks: " << sinks.size() << "\n";

	    /* Potential optimization:
	     * Try to reduce to product whereever possible.
	     */
	    {
	      VSetSet gsources, gsinks;
	      collapseOptimization(A.sts, gsources, gsinks);
	      ofs << "num_source_groups: " << sources.size() << " " << gsources.size() << "\n";
	      ofs << "num_sink_groups: " << sinks.size() << " " << gsinks.size() << "\n";
	    }
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

