#include "FenceReplacementPass.h"

#include <vector>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>

namespace clou {

  bool FenceReplacementPass::runOnFunction(llvm::Function& F) {
    std::vector<llvm::FenceInst *> todo;
    for (llvm::BasicBlock& B : F) {
      for (llvm::Instruction& I : B) {
	if (auto *FI = llvm::dyn_cast<llvm::FenceInst>(&I)) {
	  if (FI->getOrdering() == llvm::AtomicOrdering::Acquire) {
	    todo.push_back(FI);
	  }
	}
      }
    }

    for (llvm::FenceInst *FI : todo) {
      // replace with X86 LFENCE
      llvm::IRBuilder<> IRB {FI};
      llvm::Instruction *new_inst = createInst(IRB);
      FI->replaceAllUsesWith(new_inst);
      FI->eraseFromParent();
    }
	
    return !todo.empty();
  }

  
}
