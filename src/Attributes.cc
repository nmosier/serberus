#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/Clou/Clou.h>
#include <llvm/Support/WithColor.h>

#include "clou/util.h"
#include "clou/analysis/ConstantAddressAnalysis.h"

namespace clou {
  namespace {

    struct AttributesPass final : public llvm::FunctionPass {
      static inline char ID = 0;
      AttributesPass(): llvm::FunctionPass(ID) {}

      void getAnalysisUsage(llvm::AnalysisUsage& AU) const override {
	AU.addRequired<ConstantAddressAnalysis>();
	AU.setPreservesCFG();
      }

      bool runOnFunction(llvm::Function& F) override {
	if (enabled.fps) {
	  F.addFnAttr("noredzone");
	}

	if (util::functionIsDirectCallOnly(F))
	  F.addFnAttr(llvm::Attribute::NoCfCheck);

	const char *stack_protector_attrs[] = {"ssp", "sspstrong", "sspreq"};
	for (const char *stack_protector_attr : stack_protector_attrs) {
	  if (F.hasFnAttribute(stack_protector_attr)) {
	    llvm::WithColor::warning() << "compiling with stack protectors (e.g., -fstack-protector), which may introduce leakage\n";
	  }
	}

	// Use Constant Address Analysis to tag stores for use during code generation.
	{
	  auto& CAA = getAnalysis<ConstantAddressAnalysis>();
	  auto& ctx = F.getContext();
	  const llvm::StringRef Key = "llsct.ca";
	  llvm::MDNode *Value = llvm::MDNode::get(ctx, {});
	    
	  for (llvm::StoreInst& SI : util::instructions<llvm::StoreInst>(F))
	    if (CAA.isConstantAddress(SI.getPointerOperand()))
	      SI.setMetadata(Key, Value);
	}
	    
	return true;
      }
    };

    static llvm::RegisterPass<AttributesPass> X {"clou-attributes-pass", "Clou's Attributes Pass"};
    static util::RegisterClangPass<AttributesPass> Y;
    
  }
}
