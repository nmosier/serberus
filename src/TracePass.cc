

#include "FenceReplacementPass.h"
#include "util.h"

namespace clou {
  namespace {

    struct TracePass final : public FenceReplacementPass {
      static inline char ID = 0;
      TracePass(): FenceReplacementPass(ID) {}


      static llvm::FunctionCallee getTraceFunc(llvm::IRBuilder<>& IRB) {
	llvm::Module *M = IRB.GetInsertBlock()->getModule();
	llvm::FunctionType *fty = llvm::FunctionType::get(IRB.getVoidTy(), false);
	return M->getOrInsertFunction("clou_trace", fty);
      }

      llvm::Instruction *createInst(llvm::IRBuilder<>& IRB) const override {
	return IRB.CreateCall(getTraceFunc(IRB));
      }
    };

    llvm::RegisterPass<TracePass> X {"trace-pass", "Trace Pass"};
    util::RegisterClangPass<TracePass> Y {
      llvm::PassManagerBuilder::EP_OptimizerLast,
      llvm::PassManagerBuilder::EP_EnabledOnOptLevel0,      
    };
    
  }
}
