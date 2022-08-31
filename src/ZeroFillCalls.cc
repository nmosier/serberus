#include <llvm/Pass.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/IR/LegacyPassManager.h>

#include <cassert>
#include <array>
#include <sstream>
#include <string>

#include "InstructionPass.h"
#include "util.h"

namespace clou {

  struct ZeroFillCalls final : InstructionPass {
    static inline char ID = 0;

    ZeroFillCalls(): InstructionPass(ID) {}

    std::map<std::string, int> stats;

    bool shouldZeroFillCall(llvm::CallBase *C) {
      if (C->isIndirectCall()) {
	stats["indirect"]++;
	return true;
      }
      if (C->isInlineAsm()) {
	stats["asm"]++;
	return false; // TODO: Verify that this is OK.
      }
      if (llvm::isa<llvm::DbgInfoIntrinsic>(C)) {
	stats["dbg"]++;
	return false;
      }
      if (llvm::IntrinsicInst *II = llvm::dyn_cast<llvm::IntrinsicInst>(C)) {
	llvm::StringRef name = llvm::Intrinsic::getName(II->getIntrinsicID());
	stats[name.str()]++;
	// Assume x86 intrinsics will never lower to function calls.
	if (name.startswith("llvm.x86.")) {
	  return false;
	}
	return true;
      }
      llvm::Function *F = util::getCalledFunction(C);
      assert(F);
      if (F->isDeclaration()) {
	stats["declaration"]++;
	return true;
      }
      if (F->isVarArg()) {
	stats["variadic"]++;
	return true;
      }
      stats["regular"]++;
      return false;
    }

    llvm::InlineAsm *getInlineAsm(llvm::FunctionType *func_ty) {
      struct GReg {
	const char *q;
	const char *d;
      };

      static const GReg gregs[] = {
	{"rdi", "edi"},
	{"rsi", "esi"},
	{"rdx", "edx"},
	{"rcx", "ecx"},
	{"r8",  "r8d"},
	{"r9",  "r9d"},
      };

      std::stringstream asm_str;
      for (const GReg& greg : gregs) {
	asm_str << "xor " << greg.d << "," << greg.d << "\n";
      }
      asm_str << "vzeroall\n";

      std::stringstream constraints;
      for (const GReg& greg : gregs) {
	constraints << "~{" << greg.q << "},";
      }
      constraints << "~{flags}";

      return llvm::InlineAsm::get(func_ty, asm_str.str(), constraints.str(), false, false, llvm::InlineAsm::AD_Intel);
    }

    // TODO: This is broken. Apparently some arguments to memcpy.inline must be immediates.
    static bool transformMemIntrinsic(llvm::MemIntrinsic *MI) {
      if (MI->getIntrinsicID() == llvm::Intrinsic::memcpy) {
	llvm::FunctionType *ty = MI->getCalledFunction()->getFunctionType();
	llvm::Function *memcpy_inline = llvm::Intrinsic::getDeclaration(MI->getModule(), llvm::Intrinsic::memcpy_inline, ty->params().drop_back());
	assert(ty == memcpy_inline->getFunctionType());
	MI->setCalledFunction(memcpy_inline);
	return true;
      }
      return false;
    }

    bool runOnInstruction(llvm::Instruction& I) override {
      llvm::errs() << "here\n";
      
      llvm::CallBase *C = llvm::dyn_cast<llvm::CallBase>(&I);
      if (!C) {
	return false;
      }
      if (!shouldZeroFillCall(C)) {
	return false;
      }

#if 0
      // Transform llvm.{memcpy,memset} -> llvm.{memcpy,memset}.inline
      if (llvm::MemIntrinsic *MI = llvm::dyn_cast<llvm::MemIntrinsic>(C)) {
	if (transformMemIntrinsic(MI)) {
	  return true;
	}
      }
#endif
      
      llvm::Type *void_ty = llvm::Type::getVoidTy(I.getContext());
      llvm::FunctionType *func_ty = llvm::FunctionType::get(void_ty, false);
      llvm::InlineAsm *inline_asm = getInlineAsm(func_ty);
      llvm::IRBuilder<> IRB (C);
      IRB.CreateCall(func_ty, inline_asm);

      return true;
    }

    void print(llvm::raw_ostream& os, const llvm::Module *M) const override {
      for (auto [key, value] : stats) {
	os << key << ": " << value << "\n";
      }
    }
  };

  namespace {
    llvm::RegisterPass<ZeroFillCalls> X {"zero-fill-calls", "Zero Fill Calls Pass"};

    void registerPass(const llvm::PassManagerBuilder&, llvm::legacy::PassManagerBase& PM) {
      PM.add(new ZeroFillCalls());
    }
    llvm::RegisterStandardPasses Y {
      llvm::PassManagerBuilder::EP_EnabledOnOptLevel0,
      registerPass,
    };
    llvm::RegisterStandardPasses Z {
      llvm::PassManagerBuilder::EP_FullLinkTimeOptimizationLast,
      registerPass,
    };
  }
  
}
