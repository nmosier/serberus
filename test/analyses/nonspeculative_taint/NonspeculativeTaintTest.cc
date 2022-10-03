#include "NonspeculativeTaint.h"

#ifdef NDEBUG
# undef NDEBUG
#endif
#ifdef NASSERT
# undef NASSERT
#endif

#include <cassert>

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/InstIterator.h>

namespace clou {
  namespace {

    struct NonspeculativeTaintTest final : public llvm::FunctionPass {
      static inline char ID = 0;
      NonspeculativeTaintTest(): llvm::FunctionPass(ID) {}

      void getAnalysisUsage(llvm::AnalysisUsage& AU) const override {
	AU.addRequired<NonspeculativeTaint>();
	AU.setPreservesAll();
      }

      bool runOnFunction(llvm::Function& F) override {
	auto& NST = getAnalysis<NonspeculativeTaint>();

	// All arguments are nonspeculatively public.
	for (auto& A : F.args()) {
	  assert(!NST.secret(&A));
	}

	// All pointers are nonspeculatively public.
	for (auto& I : llvm::instructions(F)) {
	  if (I.getType()->isPointerTy()) {
	    assert(!NST.secret(&I));
	  }
	}
	
	return false;
      }
    };

    llvm::RegisterPass<NonspeculativeTaintTest> X {"clou-nonspeculative-taint-test", "Clou's Nonspeculative Taint Test Pass"};
    
  }
}
