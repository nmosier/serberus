#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Use.h>
#include <llvm/IR/User.h>

#include "util.h"
#include "Mitigation.h"

namespace clou {
  namespace {

    struct BaselinePass final : public llvm::FunctionPass {
      static inline char ID = 0;
      BaselinePass(): llvm::FunctionPass(ID) {}

      bool runOnFunction(llvm::Function& F) override {
	if (util::functionIsDirectCallOnly(F)) {
	  return false;
	}
	
	CreateMitigation(&F.getEntryBlock().front(), "baseline-spectre-v2");
	return true;
      }
    };


    llvm::RegisterPass<BaselinePass> X {"baseline-spectre-v2", "Baseline Spectre v2 Pass"};
    util::RegisterClangPass<BaselinePass> Y;
    
  }
}
