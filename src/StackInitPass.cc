#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>

#include "clou/util.h"

namespace clou {
  namespace {

    struct StackInitPass final : public llvm::FunctionPass {
      static inline char ID = 0;
      StackInitPass(): llvm::FunctionPass(ID) {}

      bool runOnFunction(llvm::Function& F) override {
	const auto orig_icount = F.getInstructionCount();
	llvm::DataLayout DL(F.getParent());
	for (llvm::Instruction& I : llvm::instructions(F)) {
	  if (auto *AI = llvm::dyn_cast<llvm::AllocaInst>(&I)) {
	    llvm::IRBuilder IRB(AI->getNextNode());
	    llvm::Constant *EltSize = IRB.getInt64(DL.getTypeAllocSize(AI->getAllocatedType()));
	    if (AI->isArrayAllocation()) {
	      llvm::Constant *Zero = IRB.getInt8(0);
	      llvm::Value *Size = IRB.CreateMul(EltSize, AI->getArraySize());
	      llvm::Value *Ptr = IRB.CreateBitCast(AI, IRB.getInt8PtrTy());
	      IRB.CreateMemSet(Ptr, Zero, Size, llvm::MaybeAlign(), /*isVolatile*/true);
	    } else if (AI->getAllocatedType()->isIntOrPtrTy()) {
	      llvm::Constant *Zero = llvm::Constant::getNullValue(AI->getAllocatedType());
	      IRB.CreateStore(Zero, AI, /*isVolatile*/true);
	    } else {
	      // Memset struct or array or something
	      llvm::Constant *Zero = IRB.getInt8(0);
	      llvm::Value *Ptr = IRB.CreateBitCast(AI, IRB.getInt8PtrTy());
	      IRB.CreateMemSet(Ptr, Zero, EltSize, llvm::MaybeAlign(), /*isVolatile*/true);
	    }
	  }
	}
	return F.getInstructionCount() != orig_icount;
      }
    };
    
    const llvm::RegisterPass<StackInitPass> X {"llsct-stack-init-pass", "LLSCT's Stack Initialization Pass"};
    const util::RegisterClangPass<StackInitPass> Y;
  }
}
