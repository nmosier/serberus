#include "AllocaInitPass.h"

#include <map>
#include <set>
#include <numeric>
#include <queue>

#include <llvm/IR/Instructions.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/DependenceAnalysis.h>
#include <llvm/IR/Dominators.h>

#include "util.h"
#include "NonspeculativeTaint.h"
#include "SpeculativeTaint2.h"

namespace clou {

  char AllocaInitPass::ID = 0;

  AllocaInitPass::AllocaInitPass(): llvm::FunctionPass(ID) {}

  void AllocaInitPass::getAnalysisUsage(llvm::AnalysisUsage& AU) const {
    AU.addRequired<llvm::AAResultsWrapperPass>();
    AU.addRequired<NonspeculativeTaint>();
    AU.addRequired<SpeculativeTaint>();
    AU.setPreservesAll();
  }

  void AllocaInitPass::getAccessSets(llvm::Function& F, llvm::AliasAnalysis& AA) {
    for (llvm::BasicBlock& B : F) {
      for (llvm::Instruction& I : B) {
	if (llvm::CallBase *CB = llvm::dyn_cast<llvm::CallBase>(&I)) {
	  if (CB->mayReadOrWriteMemory()) {
	    for (auto& [AI, result] : results) {
	      if (CB->mayReadFromMemory()) {
		result.loads.insert(CB);
	      }
	      if (CB->mayWriteToMemory()) {
		result.stores.insert(CB);
	      }
	    }
	  }
	} else if (llvm::isa<llvm::LoadInst, llvm::StoreInst>(&I)) {
	  for (auto& [AI, result] : results) {
	    if (AA.alias(AI, llvm::getLoadStorePointerOperand(&I)) != llvm::AliasResult::NoAlias) {
	      if (llvm::isa<llvm::LoadInst>(&I)) {
		result.loads.insert(&I);
	      }
	      if (llvm::isa<llvm::StoreInst>(&I)) {
		result.stores.insert(&I);
	      }
	    }
	  }
	} else {
	  if (I.mayReadOrWriteMemory()) {
	    unhandled_value(I);
	  }
	}
      }
    }
  }

  AllocaInitPass::ISet AllocaInitPass::pruneAccesses(llvm::Function& F, NonspeculativeTaint& NST, SpeculativeTaint& ST,
						     const ISet& in) {
    ISet out;

    std::set<llvm::Instruction *> seen;
    std::queue<llvm::Instruction *> todo;
    todo.push(&F.getEntryBlock().front());

    while (!todo.empty()) {
      llvm::Instruction *I = todo.front();
      todo.pop();
      if (seen.insert(I).second) {
	if (in.contains(I) && !NST.secret(I) && !ST.secret(I)) {
	  out.insert(I);
	} else {
	  for (llvm::Instruction *succ : llvm::successors_inst(I)) {
	    todo.push(succ);
	  }
	}
      }
    }

    return out;
  }

  bool AllocaInitPass::runOnFunction(llvm::Function& F) {
    results.clear();
    
    auto& AA = getAnalysis<llvm::AAResultsWrapperPass>().getAAResults();
    auto& NST = getAnalysis<NonspeculativeTaint>();
    auto& ST = getAnalysis<SpeculativeTaint>();
    
    /* Really, just need to collect the set of instructions that may alias.
     * Then we can prune using dominator tree.
     */

    /* Dataflow Analysis
     * Track the candidate set of first initializations of each alloca.
     * Also need to analyze the set of pointers that may point to each alloca.
     *
     * 1. Analyze set of pointers that may point to each alloca.
     */

    // Get complete set of possible dependencies
    
    // Populate map with alloca's
    for (auto& B : F) {
      for (auto& I : B) {
	if (auto *AI = llvm::dyn_cast<llvm::AllocaInst>(&I)) {
	  results.emplace(AI, Result());
	}
      }
    }
    
    // Get full load and store sets for alloca's
    getAccessSets(F, AA);
    
    // Prune load and store sets
    for (auto& [_, result] : results) {
      result.loads = pruneAccesses(F, NST, ST, result.loads);
      result.stores = pruneAccesses(F, NST, ST, result.stores);
    }

    return false;
  }

  static llvm::RegisterPass<AllocaInitPass> X {"clou-alloca-init", "Clou's Alloca Init Pass", true, true};
  
}
