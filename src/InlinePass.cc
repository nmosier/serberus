#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Attributes.h>

#include "clou/util.h"
#include "clou/analysis/SpeculativeTaintAnalysis.h"
#include "clou/analysis/LeakAnalysis.h"

namespace clou {
  namespace {
    struct InlinePass final : public llvm::FunctionPass {
      static inline char ID = 0;
      InlinePass(): llvm::FunctionPass(ID) {}

      void getAnalysisUsage(llvm::AnalysisUsage& AU) const override {
	AU.addRequired<SpeculativeTaint>();
	AU.addRequired<LeakAnalysis>();
	AU.setPreservesCFG();
      }

      bool runOnFunction(llvm::Function& F) override {
	auto& LA = getAnalysis<LeakAnalysis>();
	auto& ST = getAnalysis<SpeculativeTaint>();

	for (llvm::Instruction& I : llvm::instructions(F)) {
	  if (LA.mayLeak(&I) && ST.secret(&I)) {
	    // We found a leaked speculative secret, so we won't bother inlining
	    llvm::errs() << getPassName() << ": " << F.getName() << " leaks the speculative secret: " << I << "\n";
	    return false;
	  }
	}

	F.addFnAttr(llvm::Attribute::AlwaysInline);

	llvm::errs() << getPassName() << ": marking " << F.getName() << " as always_inline\n";

	return true;
      }
    };

    llvm::RegisterPass<InlinePass> X {"clou-inline-hints", "LLVM-SCT's Inlining Pass"};
    util::RegisterClangPass<InlinePass> Y {
      llvm::PassManagerBuilder::EP_EarlyAsPossible,
      llvm::PassManagerBuilder::EP_EnabledOnOptLevel0,
    };
  }
}
