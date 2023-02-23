#include <llvm/Pass.h>
#include <llvm/IR/Instructions.h>

#include "clou/util.h"

namespace clou {
  namespace {

    struct FramePromotion final : public llvm::ModulePass {
      static inline char ID = 0;
      FramePromotion(): llvm::ModulePass(ID) {}

      bool runOnModule(llvm::Module& M) override {
	bool changed = false;
	for (llvm::Function& F : M) {
	  if (!F.isDeclaration()) {
	    changed |= runOnFunction(F);
	  }
	}
	return changed;
      }
      
      bool runOnFunction(llvm::Function& F) {
	if (!util::doesNotRecurse(F))
	  return false;
	
	llvm::Module& M = *F.getParent();
	bool changed = false;
	std::vector<llvm::AllocaInst *> worklist;
	for (llvm::AllocaInst& AI : util::instructions<llvm::AllocaInst>(F))
	  if (AI.isStaticAlloca())
	    worklist.push_back(&AI);
	for (auto *AI : worklist) {
	  if (AI->isStaticAlloca()) {
	    llvm::Type *Ty = AI->getAllocatedType();
	    llvm::GlobalVariable *GV = new llvm::GlobalVariable(M, Ty, false, llvm::GlobalVariable::InternalLinkage,
								llvm::Constant::getNullValue(Ty));
	    GV->setAlignment(AI->getAlign());
	    GV->setDSOLocal(true);
	    AI->replaceAllUsesWith(GV);
	    AI->eraseFromParent();
	    changed = true;
	  }
	}
	return changed;
      }
    };

    static llvm::RegisterPass<FramePromotion> X {"clou-frame-promition", "LLSCT's Frame Promotion Pass", false, false};
    static util::RegisterClangPass<FramePromotion> Y;
    
  }
}
