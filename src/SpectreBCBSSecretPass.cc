#include <vector>
#include <set>
#include <ostream>
#include <string>
#include <fstream>

#include <llvm/Pass.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Argument.h>

#include "NonspeculativeTaint.h"
#include "util.h"
#include "min-cut.h"
#include "metadata.h"

namespace clou {
  namespace {

    llvm::cl::opt<std::string> emit_dot("emit-dot",
					llvm::cl::desc("Emit dot graphs"),
					llvm::cl::init(""),
					llvm::cl::OptionHidden::NotHidden);    

    struct Node {
      llvm::Value *V;

      Node() = default;
      Node(llvm::Value *V): V(V) {}

      bool operator<(const Node& o) const { return V < o.V; }
      bool operator==(const Node& o) const { return V == o.V; }
    };

    std::ostream& operator<<(std::ostream& os, const Node& node) {
      llvm::raw_os_ostream(os) << *node.V;
      return os;
    }

    struct SpectreBCBSPass final : public llvm::FunctionPass {
      static inline char ID = 0;
      SpectreBCBSPass(): llvm::FunctionPass(ID) {}

      void getAnalysisUsage(llvm::AnalysisUsage& AU) const override {
	AU.addRequired<NonspeculativeTaint>();
      }

      template <class OutputIt1, class OutputIt2>
      void get_store_address_sources(llvm::Value *V, OutputIt1& root_out, OutputIt2& interior_out, std::set<llvm::Value *>& seen) {
	if (!seen.insert(V).second) { return; }
	if (auto *I = llvm::dyn_cast<llvm::Instruction>(V)) {
	  if (I->mayReadFromMemory()) {
	    *root_out++ = I;
	  } else {
	    *interior_out++ = I;
	    for (llvm::Value *V : I->operands()) {
	      get_store_address_sources(V, root_out, interior_out, seen);
	    }
	  }
	} else if (llvm::isa<llvm::Argument>(V)) {
	  *root_out++ = V;
	} else if (llvm::isa<llvm::Constant>(V)) {
	  // ignore
	} else {
	  unhandled_value(*V);
	}
      }

      template <class OutputIt1, class OutputIt2>
      void get_store_address_sources(llvm::Value *V, OutputIt1 root_out, OutputIt2 interior_out) {
	std::set<llvm::Value *> seen;
	get_store_address_sources(V, root_out, interior_out, seen);
      }

      template <class InputIt>
      void markIntermediatesNoSpill(llvm::Instruction *root, InputIt interior_begin, InputIt interior_end) {
	// Find all instructions we can reach before hitting fences
	std::queue<llvm::Instruction *> todo;
	todo.push(root);
	std::set<llvm::Instruction *> seen;
	while (!todo.empty()) {
	  llvm::Instruction *I = todo.front();
	  todo.pop();
	  if (seen.insert(I).second) {
	    if (!llvm::isa<llvm::FenceInst>(I)) {
	      for (llvm::Instruction *pred : llvm::predecessors(I)) {
		todo.push(pred);
	      }
	    }
	  }
	}

	// Find which interiors were found
	for (InputIt interior_it = interior_begin; interior_it != interior_end; ++interior_it) {
	  llvm::Instruction *I = *interior_it;
	  if (seen.contains(I)) {
	    md::setMetadataFlag(I, md::nospill);
	  }
	}
      }

      bool runOnFunction(llvm::Function& F) override {
	auto& NST = getAnalysis<NonspeculativeTaint>();
	
	// find stores with secret operands
	std::vector<llvm::StoreInst *> secret_oob_stores;
	for_each_inst<llvm::StoreInst>(F, [&] (llvm::StoreInst *store) {
	  if (!md::getMetadataFlag(store, md::speculative_inbounds) && NST.secret(store->getValueOperand())) {
	    secret_oob_stores.push_back(store);
	  }
	});

	using Alg = FordFulkersonMulti<Node, int>;
	Alg::Graph G;

	llvm::DominatorTree DT (F);
	llvm::LoopInfo LI (DT);

	// Add CFG to graph
	for (llvm::BasicBlock& B : F) {
	  for (llvm::Instruction& dst : B) {
	    for (llvm::Instruction *src : llvm::predecessors(&dst)) {
	      if (llvm::isa<llvm::FenceInst>(src)) {
		// don't add, since already mitigated
	      } else {
		G[src][&dst] = 1; // compute_edge_weight(dst, DT, LI);
	      }
	    }
	  }
	}

	// Add arguments
	for (llvm::Argument& A : F.args()) {
	  G[&A][&F.getEntryBlock().front()] = 1; // TODO: compute edge weight
	}

	// Source-transmitter structs
	std::vector<Alg::ST> sts;
	std::set<llvm::Instruction *> interior_sources;
	for (llvm::StoreInst *SI : secret_oob_stores) {
	  Alg::ST st;
	  st.t = SI;

	  // add definitions (Spectre v4, since which may lower to stores)
	  get_store_address_sources(SI->getPointerOperand(), std::inserter(st.s, st.s.end()), std::inserter(interior_sources, interior_sources.end()));
	  if (st.s.empty()) {
	    llvm::errs() << "here\n";
	    continue;
	  }

	  // add branches that sit between store and input definitions (Spectre v1)
	  {
	    const auto& inputs = st.s;
	    std::queue<llvm::Instruction *> todo;
	    todo.push(SI);
	    std::set<llvm::Instruction *> seen;

	    // collect all instructions between store and inputs
	    while (!todo.empty()) {
	      llvm::Instruction *I = todo.front();
	      todo.pop();
	      if (seen.insert(I).second) {
		if (!inputs.contains(I)) {
		  for (llvm::Instruction *pred : llvm::predecessors(I)) {
		    todo.push(pred);
		  }
		}
	      }
	    }
	    
	    // copy terminators with 2+ successors to sources
	    for (llvm::Instruction *I : seen) {
	      if (I->isTerminator() && I->getNumSuccessors() > 1) {
		st.s.insert(I);
	      }
	    }
	  }
	  
	  sts.push_back(std::move(st));
	}

	// Run algorithm
	std::vector<std::pair<Node, Node>> cut_edges;
	Alg::run(G, sts.begin(), sts.end(), std::back_inserter(cut_edges));

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
	    
	    std::set<Node> special;
	    for (const Alg::ST& st : sts) {
	      special.insert(st.t);
	      std::copy(st.s.begin(), st.s.end(), std::inserter(special, special.end()));
	    }
	    
            for (const auto& [node, i] : nodes) {
                f << "node" << i << " [label=\"" << node << "\"";
		if (special.contains(node)) {
		  f << ", color=\"blue\"";
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
	  /* Corner case: Destination is phi node.
	   * It should be OK to always insert after phi in this case.
	   */
	  llvm::Instruction *ins = llvm::cast<llvm::Instruction>(dst.V);
	  while (llvm::isa<llvm::PHINode>(ins)) {
	    ins = ins->getNextNode();
	  }
	  llvm::IRBuilder<> IRB (ins);
	  IRB.CreateFence(llvm::AtomicOrdering::Acquire);
	}

	// Mark nospills
	for (const auto& st : sts) {
	  auto *I = llvm::cast<llvm::Instruction>(st.t.V);
	  markIntermediatesNoSpill(I, interior_sources.begin(), interior_sources.end());
	}
	
	return true;
      }
    };

    llvm::RegisterPass<SpectreBCBSPass> X {"spectre-bcbs-secret", "Spectre BCBS Secret Pass"};
    util::RegisterClangPass<SpectreBCBSPass> Y {
      llvm::PassManagerBuilder::EP_OptimizerLast,
      llvm::PassManagerBuilder::EP_EnabledOnOptLevel0
    };
    
  }
}
