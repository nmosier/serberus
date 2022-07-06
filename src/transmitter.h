#pragma once

#include <llvm/IR/Instructions.h>
#include <set>

struct Transmitter {
    llvm::Instruction *I;
    std::set<llvm::Value *> ops;
};

template <class OutputIt>
OutputIt get_transmitter_sensitive_operands(llvm::Instruction *I,
                                            OutputIt out) {
  if (llvm::isa<llvm::LoadInst, llvm::StoreInst>(I)) {
    *out++ = llvm::getPointerOperand(I);
  } else if (llvm::BranchInst *BI = llvm::dyn_cast<llvm::BranchInst>(I)) {
    if (BI->isConditional()) {
      *out++ = BI->getCondition();
    }
  } else if (llvm::CallBase *C = llvm::dyn_cast<llvm::CallBase>(I)) {
    for (llvm::Value *op : C->operands()) {
      *out++ = op;
    }
  } else if (llvm::ReturnInst *RI = llvm::dyn_cast<llvm::ReturnInst>(I)) {
    if (llvm::Value *RV = RI->getReturnValue()) {
      *out++ = RV;
    }
  }
  return out;
}

std::set<llvm::Value *>
get_transmitter_sensitive_operands(llvm::Instruction *I);

