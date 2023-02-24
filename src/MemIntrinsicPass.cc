#include <llvm/Pass.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/IRBuilder.h>

#include "clou/util.h"
#include "clou/analysis/ConstantAddressAnalysis.h"

namespace clou {
  namespace {

    struct MemIntrinsicPass final : public llvm::FunctionPass {
      static inline char ID = 0;
      MemIntrinsicPass(): llvm::FunctionPass(ID) {}

      bool handleMemIntrinsic(llvm::MemIntrinsic *MI) {
	if (!MI->use_empty())
	  return false;

	auto *MCI = llvm::dyn_cast<llvm::MemCpyInst>(MI);
	if (MCI == nullptr)
	  return false;

	if (llvm::isa<llvm::MemCpyInlineInst>(MCI))
	  return false;

	llvm::Value *Len = MI->getLength();
	if (!llvm::isa<llvm::Constant>(Len))
	  return false;
	
	llvm::IRBuilder<> IRB(MI);

#if 0
	if (auto *MSI = llvm::dyn_cast<llvm::MemSetInst>(MI)) {
	  if (llvm::isa<llvm::MemSetInlineInst>(MI))
	    return false;
	  IRB.CreateMemSetInline(MSI->getDest(), MSI->getDestAlign(), MSI->getValue(), Len, MSI->isVolatile());
	}
#endif
	if (auto *MCI = llvm::dyn_cast<llvm::MemCpyInst>(MI)) {
	  if (llvm::isa<llvm::MemCpyInlineInst>(MI))
	    return false;
	  IRB.CreateMemCpyInline(MCI->getDest(), MCI->getDestAlign(), MCI->getSource(), MCI->getSourceAlign(), Len, MCI->isVolatile());
	} else {
	  return false;
	}
	MI->eraseFromParent();
	return true;
      }

      bool runOnFunction(llvm::Function& F) override {
	std::vector<llvm::MemIntrinsic *> worklist;
	for (auto& I : util::instructions<llvm::MemIntrinsic>(F))
	  worklist.push_back(&I);

	bool changed = false;
	for (llvm::MemIntrinsic *MI : worklist) {
	  changed |= handleMemIntrinsic(MI);
	}

	return changed;
      }
    };

    llvm::RegisterPass<MemIntrinsicPass> X {"llsct-mem-intrinsic-pass", "LLSCT's Mem Intrinsic Pass"};
    util::RegisterClangPass<MemIntrinsicPass> Y;
    
  }
}
