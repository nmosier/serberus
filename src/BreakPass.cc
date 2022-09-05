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

    struct BreakPass final : public FenceReplacementPass {
      static inline char ID = 0;
      BreakPass(): FenceReplacementPass(ID) {}

      llvm::Instruction *createInst(llvm::IRBuilder<>& IRB) const override {
	return IRB.CreateIntrinsic(llvm::Intrinsic::debugtrap, {}, {});
      }
    };
    
    llvm::RegisterPass<BreakPass> X {"break-pass", "Break Pass"};
    util::RegisterClangPass<BreakPass> Y {
      llvm::PassManagerBuilder::EP_OptimizerLast,
      llvm::PassManagerBuilder::EP_EnabledOnOptLevel0,
    };
  }
}
