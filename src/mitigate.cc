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

#include <boost/graph/adjacency_list.hpp>

#include <fstream>
#include <map>
#include <sstream>

#include "util.h"

struct Mitigate final : public llvm::FunctionPass {

  static inline char ID = 0;
  Mitigate() : llvm::FunctionPass(ID) {}

  enum EdgeKind { ADDR, DATA, RF };

  using Graph = std::map<llvm::Value *, std::map<llvm::Value *, EdgeKind>>;
  using SIDToStore = std::map<unsigned, llvm::StoreInst *>;

  bool runOnFunction(llvm::Function &F) override {
    // parse store ids
    std::map<unsigned, llvm::StoreInst *> sid_to_store;
    for_each_inst<llvm::StoreInst>(F, [&sid_to_store](llvm::StoreInst *SI) {
      if (llvm::MDNode *MD = SI->getMetadata("clou.sid")) {
        assert(MD->getNumOperands() == 1);
        llvm::Metadata *M = MD->getOperand(0).get();
        llvm::MDString *MDS = llvm::cast<llvm::MDString>(M);
        llvm::StringRef SR = MDS->getString();
        unsigned sid = std::stoul(SR.str());
        sid_to_store[sid] = SI;
      }
    });

    // construct graph
    Graph graph;

    std::vector<llvm::Instruction *> todo;
    std::set<llvm::Instruction *> done;

#if 0
    for_each_inst<llvm::Instruction>(F, [&todo] (llvm::Instruction *I) {
      if (llvm::isa<llvm::LoadInst, llvm::StoreInst>(I) && is_speculative_secret(llvm::getPointerOperand(I))) {
	todo.push_back(I);
      }
    });

    while (!todo.empty()) {
      
    }
#endif

    // add address dependencies
    for_each_inst<llvm::LoadInst>(F, [&graph](llvm::LoadInst *LI) {
      llvm::Value *pointer = LI->getPointerOperand();
      // get load sources
      if (is_speculative_secret(pointer)) {
        for (llvm::Value *src : get_incoming_loads(pointer)) {
          if (is_speculative_secret(src)) {
            graph[src][LI] = ADDR;
          }
        }
      }
    });

    // add rf's
    for_each_inst<llvm::LoadInst>(F, [&graph,
                                      &sid_to_store](llvm::LoadInst *LI) {
      if (llvm::MDNode *MDN = LI->getMetadata("clou.rf")) {
        for (llvm::Metadata *MD : llvm::cast<llvm::MDTuple>(MDN)->operands()) {
          const unsigned sid =
              std::stoul(llvm::cast<llvm::MDString>(MD)->getString().str());
          llvm::StoreInst *SI = sid_to_store.at(sid);
          graph[SI][LI] = RF;
          if (llvm::Instruction *value_I =
                  llvm::dyn_cast<llvm::Instruction>(SI->getValueOperand())) {
            if (llvm::isa<llvm::LoadInst>(value_I)) {
              if (is_speculative_secret(value_I)) {
                graph[value_I][SI] = DATA;
              }
            } else {
              for (llvm::Value *value_V : get_incoming_loads(value_I)) {
                if (is_speculative_secret(value_V)) {
                  graph[value_V][SI] = DATA;
                }
              }
            }
          }
        }
      }
    });

    //
    std::set<llvm::Value *> nodes;
    for (const auto &p1 : graph) {
      nodes.insert(p1.first);
      for (const auto &p2 : p1.second) {
        nodes.insert(p2.first);
      }
    }
    if (nodes.empty()) {
      return false;
    }

    std::stringstream filename;
    filename << std::getenv("OUT") << "/" << F.getName().str() << ".dot";
    std::ofstream dot{filename.str()};
    dot << "digraph {\n";
    // dot << "layout=neato\n";

    for (llvm::Value *V : nodes) {
      dot << "node" << V << " [label=\"" << *V << "\"];\n";
    }

    for (const auto &p1 : graph) {
      for (const auto &p2 : p1.second) {
        dot << "node" << p1.first << " -> node" << p2.first << " [label=\"";
        switch (p2.second) {
        case ADDR:
          dot << "addr";
          break;
        case DATA:
          dot << "data";
          break;
        case RF:
          dot << "rf";
          break;
        }
        dot << "\"];\n";
      }
    }

    dot << "}\n";

#if 0
    // print out graph
    for (const auto& p1 : graph) {
      llvm::errs() << "src: " << *p1.first << "\n";
      for (const auto& p2 : p1.second) {
	llvm::errs() << "  " << *p2.first << "\n";
      }
    }

    /* Algorithm:
     * While true:
     *  For each (s,t) pair:
     *    Depth-first search from s to t.
     *    For each path found:
     *      Add 1 to each edge's count.
     *    If no paths found: mark (s,t) pair as done.
     *  If all (s,t) pairs done: break.
     *  Remove edge with highest count.
     */
    std::set<llvm::Instruction *> transmitters;
    std::set<llvm::Value *> sources;
    for (const auto& p1 : graph) {
      sources.insert(p1.first);
    }
    for (const auto& p1 : graph) {
      for (const auto& p2 : p1.second) {
	if (p2.second == ADDR) {
	  transmitters.insert(llvm::cast<llvm::Instruction>(p2.first));
	}
	sources.erase(p2.first);
      }
    }

    if (transmitters.size() > 1) {
      llvm::errs() << F.getName() << "\n";
    llvm::errs() << "Transmitters:\n";
    for (llvm::Instruction *T : transmitters) {
      llvm::errs() << "  " << *T << "\n";
    }

    llvm::errs() << "Sources:\n";
    for (llvm::Value *S : sources) {
      llvm::errs() << "  " << *S << "\n";
    }
    }
#endif

    unsigned fences = 0;

#if 0
    // fence all transmitters
    for_each_inst<llvm::Instruction>(F, [&fences] (llvm::Instruction *I) {
      llvm::Value *C = nullptr;
      if (llvm::isa<llvm::LoadInst, llvm::StoreInst>(I)) {
	C = llvm::getPointerOperand(I);
      } else if (llvm::BranchInst *BI = llvm::dyn_cast<llvm::BranchInst>(I)) {
	if (BI->isConditional()) {
	  C = BI->getCondition();
	}
      }

      if (C == nullptr) {
	return;
      }

      if (is_speculative_secret(C)) {
	llvm::IRBuilder<> IRB (I);
	IRB.CreateFence(llvm::AtomicOrdering::Acquire);
	++fences;
      }
    });
#endif

#if 0
    for_each_inst<llvm::CallBase>(F, [&fences] (llvm::CallBase *C) {
      bool res = false;
      for (llvm::Value *arg : C->args()) {
	res |= is_speculative_secret(arg);
      }
      bool en = true;
      if (llvm::Function *F = C->getCalledFunction()) {
	if (!F->isDeclaration()) {
	  en = false;
	}
      }
      if (res && en) {
	++fences;
      }
    });
#endif

#if 0
    for_each_inst<llvm::StoreInst>(F, [&fences] (llvm::StoreInst *SI) {
      if (has_incoming_addr(SI->getPointerOperand()) && is_speculative_secret(SI->getValueOperand())) {
	++fences;
      }                  
    });
#endif

    llvm::outs() << F.getName() << " " << fences << "\n";

    return true;
  }
};

namespace {
llvm::RegisterPass<Mitigate> X{"mitigate", "Clou's Spectre Mitigation Pass"};
}
