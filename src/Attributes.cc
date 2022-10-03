#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/Clou/Clou.h>
#include <llvm/Support/WithColor.h>

#include "clou/util.h"

namespace clou {
  namespace {

    struct AttributesPass final : public llvm::FunctionPass {
      static inline char ID = 0;
      AttributesPass(): llvm::FunctionPass(ID) {}

      bool runOnFunction(llvm::Function& F) override {
	if (stack_mitigation_mode == StackMitigationMode::FunctionPrivateStacks) {
	  F.addFnAttr("noredzone");
	}

	const char *stack_protector_attrs[] = {"ssp", "sspstrong", "sspreq"};
	for (const char *stack_protector_attr : stack_protector_attrs) {
	  if (F.hasFnAttribute(stack_protector_attr)) {
	    llvm::WithColor::warning() << "compiling with stack protectors (e.g., -fstack-protector), which may introduce leakage\n";
	  }
	}
	
	return true;
      }
    };

    llvm::RegisterPass<AttributesPass> X {"clou-attributes-pass", "Clou's Attributes Pass"};
    util::RegisterClangPass<AttributesPass> Y;
    
  }
}
