#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <vector>
#include <variant>

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

#include "MinCutSMT.h"
#include "util.h"
#include "Transmitter.h"
#include "CommandLine.h"
#include "Mitigation.h"
#include "Log.h"
#include "SpeculativeTaint2.h"
#include "NonspeculativeTaint.h"

namespace clou {
  namespace {

    constexpr bool StackMitigations = true;

    using ISet = std::set<llvm::Instruction *>;
    using VSet = std::set<llvm::Value *>;
    using IMap = std::map<llvm::Instruction *, ISet>;

    struct Node {
      llvm::Value *V;
      Node() {}
      Node(llvm::Value *V): V(V) {}
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
	      return true;
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
	    for (const TransmitterOperand& op : get_transmitter_sensitive_operands(&I, false)) {
	      if (ST.secret(op.V)) {
		auto *op_I = llvm::cast<llvm::Instruction>(op.V);
		out[&I].insert(op_I);
	      }
	    }
	  }
	}
      }
    
      bool runOnFunction(llvm::Function &F) override {
	auto& NST = getAnalysis<NonspeculativeTaint>();
	auto& ST = getAnalysis<SpeculativeTaint>();

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

	// EXPERIMENTAL: Create ST-pairs for {entry, return}.
	for (auto& B : F) {
	  for (auto& I : B) {
	    if (llvm::isa<llvm::ReturnInst>(&I)) {
	      A.add_st({.s = &F.getEntryBlock().front(), .t = &I});
	    }
	  }
	}

	// EXPERIMENTAL: Create ST-pairs for {arg-def, arg-use}
	for (llvm::Argument& arg : F.args()) {
	  for (llvm::Use& use : arg.uses()) {
	    llvm::Instruction *user = llvm::cast<llvm::Instruction>(use.getUser());
	    if (auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(user)) {
	      if (II->isAssumeLikeIntrinsic() || llvm::isa<llvm::DbgInfoIntrinsic>(II)) {
		continue;
	      }
	    }
	    A.add_st({.s = &F.getEntryBlock().front(), .t = user});
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
	A.run();
	auto& cut_edges = A.cut_edges;	

#if 0
	if (verbose) {
	  for (const auto& st : sts) {
	    llvm::errs() << "\nSources:\n";
	    for (const auto& s : st.s) {
	      llvm::errs() << s << "\n";
	    }
	    llvm::errs() << "Transmitter: " << st.t << "\n";
	  }
	}
#endif
	
	// Output DOT graph, color cut edges
	if (!emit_dot.getValue().empty()) {
	  std::stringstream path;
	  path << emit_dot.getValue() << "/" << F.getName().str() << ".dot";
	  std::ofstream f(path.str());
	  f << "digraph {\n";

	  // collect stores + sinks
	  std::map<Node, std::size_t> nodes;
	  for (llvm::BasicBlock& B : F) {
	    for (llvm::Instruction& I : B) {
	      nodes.emplace(&I, nodes.size());
	    }
	  }
	  for (llvm::Argument& A : F.args()) {
	    nodes.emplace(&A, nodes.size());
	  }

	  std::map<Node, std::string> special;
	  for (const Alg::ST& st : A.sts) {
	    special[st.t] = "green";
	    special[st.s] = "blue";
	  }
	    
	  for (const auto& [node, i] : nodes) {
	    f << "node" << i << " [label=\"" << node << "\"";
	    if (special.contains(node)) {
	      f << ", style=filled, fontcolor=white, fillcolor=" << special[node] << "";
	    }
	    f << "];\n";
	  }
            
	  for (const auto& [u, usucc] : G) {
	    for (const auto& [v, weight] : usucc) {
	      if (weight > 0) {
		f << "node" << nodes.at(u) << " -> " << "node" << nodes.at(v) << " [label=\"" << weight << "\"";
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

	// Mitigations
	for (const auto& [src, dst] : cut_edges) {
	  CreateMitigation(llvm::cast<llvm::Instruction>(dst.V), "clou-mitigate-pass");
	}
	
        return true;
      }
    };

    llvm::RegisterPass<MitigatePass> X{"clou-mitigate",
					   "Clou Mitigation Pass"};
    util::RegisterClangPass<MitigatePass> Y;
  }
}

