#include "AllocaInitPass.h"

#include <map>
#include <set>
#include <numeric>
#include <queue>

#include <llvm/IR/Instructions.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/DependenceAnalysis.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Analysis/MemoryLocation.h>
#include <llvm/ADT/STLExtras.h>

#include "util.h"
#include "NonspeculativeTaint.h"
#include "SpeculativeTaint2.h"
#include "Frontier.h"

namespace clou {

  char AllocaInitPass::ID = 0;

  AllocaInitPass::AllocaInitPass(): llvm::FunctionPass(ID) {}

  void AllocaInitPass::getAnalysisUsage(llvm::AnalysisUsage& AU) const {
    AU.addRequired<llvm::AAResultsWrapperPass>();
    AU.addRequired<NonspeculativeTaint>();
    AU.addRequired<SpeculativeTaint>();
    AU.setPreservesAll();
  }


  bool AllocaInitPass::runOnFunction(llvm::Function& F) {
    results.clear();
    
    auto& AA = getAnalysis<llvm::AAResultsWrapperPass>().getAAResults();
    auto& NST = getAnalysis<NonspeculativeTaint>();
    auto& ST = getAnalysis<SpeculativeTaint>();

    llvm::DataLayout DL(F.getParent());

      // Iterate over all reads
    for (llvm::LoadInst& LI : util::instructions<llvm::LoadInst>(F)) {
      if (NST.secret(&LI) || ST.secret(&LI)) {
	continue;
      }

      llvm::errs() << "here\n";

      
      ISet must_alias_frontier;
      // Try to find frontier set of must-alias stores
      const bool ok = forward_frontier(&LI, [&] (llvm::Instruction *I) {
	if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(I)) {
	  if (AA.isMustAlias(&LI, SI)) {
	    return true;
	  }
	}
	return false;
      }, must_alias_frontier);

      if (ok) {
	for (llvm::AllocaInst& AI : util::instructions<llvm::AllocaInst>(F)) {
	  if (!AA.isNoAlias(&AI, LI.getPointerOperand())) {
	    auto& result = results[&AI];
	    result.loads.insert(&LI);
	    llvm::copy(must_alias_frontier, std::inserter(result.stores, result.stores.end()));
	  }
	}
      } else {
	// Fallback: see if there's one 'may alias' write on the reverse frontier.
	ISet may_alias_frontier;
	const bool ok = reverse_frontier(&LI, [&] (llvm::Instruction *I) {
	  const auto mri = AA.getModRefInfo(I, LI.getPointerOperand(),
					    llvm::LocationSize::precise(DL.getTypeStoreSize(LI.getType())));
	  return mri == llvm::ModRefInfo::Mod || mri == llvm::ModRefInfo::ModRef;
	}, may_alias_frontier);
	assert(!may_alias_frontier.empty());
	if (ok && may_alias_frontier.size() == 1) {
	  for (llvm::AllocaInst& AI : util::instructions<llvm::AllocaInst>(F)) {
	    if (!AA.isNoAlias(&AI, LI.getPointerOperand())) {
	      auto& result = results[&AI];
	      result.loads.insert(&LI);
	      llvm::copy(may_alias_frontier, std::inserter(result.stores, result.stores.end()));
	    }
	  }
	} else {
	  // Just use front of current basic block. Might be able to improve on this in the future.
	  // TODO: Optimize this.
	  for (llvm::AllocaInst& AI : util::instructions<llvm::AllocaInst>(F)) {
	    if (!AA.isNoAlias(&AI, LI.getPointerOperand())) {
	      auto& result = results[&AI];
	      result.loads.insert(&LI);
	      llvm::copy(llvm::predecessors(&LI), std::inserter(result.stores, result.stores.end()));
	    }
	  }
	}
      }
    }

    return false;
  }


  void AllocaInitPass::print(llvm::raw_ostream& os, const llvm::Module *) const {
    for (const auto& [AI, result] : results) {
      os << "Allocation: " << *AI << "\n";
      os << "Stores:\n";
      for (auto *store : result.stores) {
	os << "  " << *store << "\n";
      }
      os << "Loads:\n";
      for (auto *load : result.loads) {
	os << "  " << *load << "\n";
      }
      os << "\n";
    }
  }

  static llvm::RegisterPass<AllocaInitPass> X {"clou-alloca-init", "Clou's Alloca Init Pass", true, true};
  
}
