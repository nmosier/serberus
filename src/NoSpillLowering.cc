#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InlineAsm.h>

#include "util.h"
#include "metadata.h"

namespace clou {
  namespace {
    struct NoSpillLowering final : public llvm::FunctionPass {
      static inline char ID = 0;
      NoSpillLowering(): llvm::FunctionPass(ID) {}

      static llvm::FunctionCallee getNoSpillIntrinsic(llvm::Module *M, llvm::Type *T) {
	llvm::LLVMContext& ctx = M->getContext();
	llvm::FunctionType *fty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {T}, false);
	return llvm::InlineAsm::get(fty, "clou.nospill", "r", true);
      }

      bool runOnFunction(llvm::Function& F) override {
	bool changed = false;
	for (llvm::BasicBlock& B : F) {
	  for (llvm::Instruction *I = &B.front(); I != nullptr; I = I->getNextNode()) {
	    if (md::getMetadataFlag(I, md::nospill)) {
	      // insert call
	      llvm::IRBuilder<> IRB (B.getTerminator()); // use terminator to avoid accounting for PHI nodes
	      IRB.CreateCall(getNoSpillIntrinsic(F.getParent(), I->getType()), {I});
	      changed = true;
	    }
	  }
	}
	return changed;
      }
    };

    llvm::RegisterPass<NoSpillLowering> X {"nospill-lowering", "Clou's 'nospill' Lowering Pass"};
    util::RegisterClangPass<NoSpillLowering> Y;
  }
}
