#include "FenceReplacementPass.h"

#include <vector>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>

namespace clou {

  bool FenceReplacementPass::runOnFunction(llvm::Function& F) {
    auto& ctx = F.getContext();
    
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

    uint64_t id = 0;
    for (llvm::FenceInst *FI : todo) {
      // replace with X86 LFENCE
      llvm::IRBuilder<> IRB {FI};
      // TODO: Revert this since it doesn't seem to help
      {
	llvm::Instruction *I;
	for (I = FI; I != nullptr && !I->getDebugLoc(); I = I->getNextNode()) {}
	if (const llvm::DebugLoc& DL = I->getDebugLoc()) {
	  IRB.SetCurrentDebugLocation(DL);
	}
      }
      // IRB.SetCurrentDebugLocation(FI->getDebugLoc());
      llvm::ConstantInt *fenceid = llvm::ConstantInt::get(IRB.getInt64Ty(), id);
      llvm::ConstantDataArray *fence_str = nullptr;
      if (const auto *mdnode = fence_str->getMetadata("clou.lfencestr")) {
	llvm::StringRef str = llvm::cast<llvm::MDString>(mdnode->getOperand(0)->get())->getString();
	fence_str = llvm::ConstantDataArray::getString(IRB.getContext(), str);
      }

      llvm::Instruction *new_inst = createInst(IRB, fenceid, fence_str);
      FI->replaceAllUsesWith(new_inst);
      FI->eraseFromParent();
      new_inst->copyMetadata(*FI);

      // set lfenceid
      new_inst->setMetadata("clou.lfenceid", llvm::MDNode::get(ctx, {llvm::ConstantAsMetadata::get(fenceid)}));
      ++id;
    }
	
    return !todo.empty();
  }

  
}
