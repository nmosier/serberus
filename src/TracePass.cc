#include <llvm/Pass.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instruction.h>

#include "util.h"
#include "Mitigation.h"

namespace clou {
  namespace {

    struct TracePass final : public llvm::FunctionPass {
      static inline char ID = 0;
      TracePass(): llvm::FunctionPass(ID) {}

      static llvm::FunctionCallee getTraceFunc(llvm::IRBuilder<>& IRB) {
	llvm::Module *M = IRB.GetInsertBlock()->getModule();
	llvm::FunctionType *fty = llvm::FunctionType::get(IRB.getVoidTy(), {IRB.getInt64Ty(), IRB.getInt8PtrTy()}, false);
	return M->getOrInsertFunction("clou_trace", fty);
      }

      bool runOnFunction(llvm::Function& F) override {
	llvm::Module& M = *F.getParent();
	for (llvm::BasicBlock& B : F) {
	  for (llvm::Instruction& I : B) {
	    if (auto *MI = llvm::dyn_cast<MitigationInst>(&I)) {
	      llvm::IRBuilder<> IRB (MI);
	      IRB.SetCurrentDebugLocation(MI->getDebugLoc());
	      auto *byte_array = llvm::ConstantDataArray::getString(IRB.getContext(), MI->getDescription());
	      auto *byte_var = new llvm::GlobalVariable(M, byte_array->getType(), true, llvm::GlobalValue::PrivateLinkage, byte_array);
	      auto *byte_ptr =  IRB.CreateBitCast(byte_var, IRB.getInt8PtrTy());
	      llvm::Instruction *CI = IRB.CreateCall(getTraceFunc(IRB), {MI->getIdentifier(), byte_ptr});
	      CI->copyMetadata(*MI);
	    }
	  }
	}
	return true;
      }
    };

    llvm::RegisterPass<TracePass> X {"trace-pass", "Trace Pass"};
    util::RegisterClangPass<TracePass> Y {
      llvm::PassManagerBuilder::EP_OptimizerLast,
      llvm::PassManagerBuilder::EP_EnabledOnOptLevel0,      
    };
    
  }
}
