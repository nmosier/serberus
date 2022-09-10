#include "FenceReplacementPass.h"

#include <vector>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>

#include "Mitigation.h"

namespace clou {

  bool FenceReplacementPass::runOnFunction(llvm::Function& F) {
    auto& ctx = F.getContext();
    
    std::vector<MitigationInst *> todo;
    for (llvm::BasicBlock& B : F) {
      for (llvm::Instruction& I : B) {
	if (auto *MI = llvm::dyn_cast<MitigationInst>(&I)) {
	  todo.push_back(MI);
	}
      }
    }

    uint64_t id = 0;
    for (MitigationInst *FI : todo) {
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
      llvm::Constant *fence_str = nullptr;
      if (const auto *mdnode = FI->getMetadata("clou.lfencestr")) {
	llvm::StringRef str = llvm::cast<llvm::MDString>(mdnode->getOperand(0))->getString();
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
