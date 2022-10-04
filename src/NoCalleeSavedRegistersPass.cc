#include <array>

#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>

#include "clou/util.h"

namespace clou {
  namespace {

    struct NoCalleeSavedRegistersPass final : public llvm::ModulePass {
      static inline char ID = 0;
      NoCalleeSavedRegistersPass(): llvm::ModulePass(ID) {}

      void getAnalysisUsage(llvm::AnalysisUsage& AU) const override {
	AU.setPreservesCFG();
      }

      static const inline std::pair<const char *, const char *> attrs[] = {
	{"no_callee_saved_registers", ""},
	{"frame-pointer", "all"}
      };

      bool runOnModule(llvm::Module& M) override {
	for (const auto& [key, value] : attrs) {
	  for (llvm::Function& F : M) {
	    F.addFnAttr(key, value);
	    if (!F.isDeclaration()) {
	      for (llvm::CallBase& C : util::instructions<llvm::CallBase>(F)) {
		C.addFnAttr(llvm::Attribute::get(F.getContext(), "no_callee_saved_registers"));
	      }
	    }
	  }
	}
	
	return true;
      }
    };

    llvm::RegisterPass<NoCalleeSavedRegistersPass> X {"clou-no-callee-saved-registers", "Clou's No Callee Saved Registers Pass"};
    util::RegisterClangPass<NoCalleeSavedRegistersPass> Y;
    
  }
}
