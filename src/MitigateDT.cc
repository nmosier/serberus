#include <set>
#include <fstream>

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/IntrinsicsX86.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/raw_os_ostream.h>

#include "min-cut.h"
#include "util.h"
#include "CommandLine.h"
#include "Mitigation.h"
#include "Transmitter.h"

namespace clou {
  namespace {

    using ISet = std::set<llvm::Instruction *>;
    using IMap = std::map<llvm::Instruction *, ISet>;

    /* Approach:
     * Do dataflow analysis on entire function.
     * OOB stores and calls are treated specially.
     * OOB stores:
     *   We won't consider the value operand to be (pseudo-)transmitted.
     * Loads:
     *   We assume that all loads return secrets, due to the relaxed OOB store mitigation.
     * Calls:
     *   We treat all calls (that may lower to CALL instructions) as ...
     *
     * 
     */

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

    struct MitigateDTPass final : public llvm::FunctionPass {
      static inline char ID = 0;
      MitigateDTPass(): llvm::FunctionPass(ID) {}

      using Alg = FordFulkersonMulti<Node, int>;

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

      static bool ignoreCall(const llvm::Instruction *I) {
	if (const auto *CB = llvm::dyn_cast<llvm::CallBase>(I)) {
	  return ignoreCall(CB);
	} else {
	  return true;
	}
      }

      void analyzeTaints(llvm::Function& F, IMap& taints) const {
	IMap taints_bak;
	
	do {
	  taints_bak = taints;
	  for (auto& B : F) {
	    for (auto& I : B) {
	      if (I.getType()->isVoidTy()) {
		continue;
	      }
	      if (auto *CB = llvm::dyn_cast<llvm::CallBase>(&I)) {
		if (!ignoreCall(CB)) {
		  // Our contract ensures that function return values aren't speculative secrets
		  continue;
		}
	      }
	      if (I.mayReadFromMemory()) {
		taints[&I] = {&I};
		continue;
	      }
	      for (llvm::Value *op_V : I.operands()) {
		if (auto *op_I = llvm::dyn_cast<llvm::Instruction>(op_V)) {
		  const auto op_it = taints.find(op_I);
		  if (op_it != taints.end()) {
		    taints[&I].insert(op_it->second.begin(), op_it->second.end());
		  }
		}
	      }
	    }
	  }
	} while (taints != taints_bak);
      }

      static unsigned compute_edge_weight(llvm::Instruction *I, const llvm::DominatorTree& DT, const llvm::LoopInfo& LI) {
        float score = 1.;
        score *= instruction_loop_nest_depth(I, LI) + 1;
        score *= 1. / (instruction_dominator_depth(I, DT) + 1);
        return score * 100;
      }      

      void constructGraph(llvm::Function& F, IMap& taints, const ISet& ctrls, const std::set<llvm::StoreInst *>& stores, Alg::Graph& G, std::vector<Alg::ST>& sts) const {
	llvm::DominatorTree DT(F);
	llvm::LoopInfo LI(DT);

	// Create CFG
	for (auto& B : F) {
	  for (auto& I_dst : B) {
	    for (auto *I_src : llvm::predecessors(&I_dst)) {
	      if (llvm::isa<MitigationInst>(I_src)) {
		// don't add, since already mitigated
	      } else {
		G[I_src][&I_dst] = compute_edge_weight(&I_dst, DT, LI);
	      }
	    }
	  }
	}

	// Add arguments
	for (auto& A_src : F.args()) {
	  auto *I_dst = &F.getEntryBlock().front();
	  G[&A_src][I_dst] = compute_edge_weight(I_dst, DT, LI);
	}

	/// Create regular transmitter ST-pairs
	for (auto& B : F) {
	  for (auto& I : B) {
	    if (!I.getMetadata("clou.secure") && ignoreCall(&I)) {
	      Alg::ST st;
	      st.t = &I;
	      const auto ops = get_transmitter_sensitive_operands(&I, false);
	      for (const auto& op : ops) {
		if (auto *op_I = llvm::dyn_cast<llvm::Instruction>(op.V)) {
		  const auto& sources = taints[op_I];
		  st.s.insert(sources.begin(), sources.end());
		}
	      }
	      if (!st.s.empty()) {
		sts.push_back(std::move(st));
	      }
	    }
	  }
	}

	/// Create store-ctrl ST-pairs
	for (auto *ctrl : ctrls) {
	  Alg::ST st;
	  st.t = ctrl;
	  for (auto *SI : stores) {
	    st.s.insert(SI);
	  }
	  if (!st.s.empty()) {
	    sts.push_back(std::move(st));
	  }
	}

      }

      bool runOnFunction(llvm::Function& F) override {
	llvm::errs() << getPassName() << ": running on " << F.getName() << "\n";
	
	// Do taint analysis
	IMap taints, taints_all;
	analyzeTaints(F, taints);
	
	ISet ctrls;
	std::set<llvm::StoreInst *> stores;
	for (auto& B : F) {
	  for (auto& I : B) {
	    if (auto *CB = llvm::dyn_cast<llvm::CallBase>(&I)) {
	      if (!ignoreCall(CB)) {
		ctrls.insert(CB);
	      }
	    }
	    if (llvm::isa<llvm::ReturnInst>(&I)) {
	      ctrls.insert(&I);
	    }
	    if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I)) {
	      if (!util::isSpeculativeInbounds(SI)) {
		stores.insert(SI);
	      }
	    }
	  }
	}
	assert(!ctrls.empty()); // We should always have a return instruction, at least.

	Alg::Graph G;
	std::vector<Alg::ST> sts;
	constructGraph(F, taints, ctrls, stores, G, sts);
	std::vector<std::pair<Node, Node>> cut_edges;
	Alg::run(G, sts.begin(), sts.end(), std::back_inserter(cut_edges));

	for (const auto& st : sts) {
	  llvm::errs() << "\nSources:\n";
	  for (const auto& s : st.s) {
	    llvm::errs() << s << "\n";
	  }
	  llvm::errs() << "Transmitter: " << st.t << "\n";
	}

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
	  for (const Alg::ST& st : sts) {
	    special[st.t] = "green";
	    for (const auto& s : st.s) {
	      special[s] = "blue";
	    }
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
		if (std::find(cut_edges.begin(), cut_edges.end(), std::pair<Node, Node>(u, v)) != cut_edges.end()) {
		  f << ", color=\"red\"";
		}
		f << "];\n";
	      }
	    }
	  }
            
	  f << "}\n";	    
	}

	// Mitigate cut edges
	for (const auto& [src, dst] : cut_edges) {
	  CreateMitigation(llvm::cast<llvm::Instruction>(dst.V), "mitigate-dt");
	}

	// Mark nospills
	for (const auto& st : sts) {
	  // TODO: Need to implement this.
	}

	return true;
      }

      
    };


    llvm::RegisterPass<MitigateDTPass> X {"clou-mitigate-dt", "Clou's DT Mitigation Pass"};
    util::RegisterClangPass<MitigateDTPass> Y;
  }
}
