#include <vector>

#include <llvm/Pass.h>
#include <llvm/IR/IntrinsicsX86.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/CommandLine.h>

#include "FenceReplacementPass.h"
#include "util.h"

namespace clou {
  namespace {

    struct LfencePass final : public FenceReplacementPass {
      static inline char ID = 0;
      LfencePass(): FenceReplacementPass(ID) {}

      llvm::Instruction *createInst(llvm::IRBuilder<>& IRB, llvm::ConstantInt *lfenceid, llvm::ConstantDataArray *) const override {
	return IRB.CreateIntrinsic(llvm::Intrinsic::x86_sse2_lfence, {}, {});
      }
    };
    
    llvm::RegisterPass<LfencePass> X {"lfence-pass", "Lfence Pass"};
    util::RegisterClangPass<LfencePass> Y {
      llvm::PassManagerBuilder::EP_OptimizerLast,
      llvm::PassManagerBuilder::EP_EnabledOnOptLevel0,
    };
  }
}
