#pragma once

#include <llvm/IR/Instructions.h>

#include <set>
#include <tuple>

#include "util.h"

struct TransmitterOperand {
    enum Kind {
        TRUE,
        PSEUDO,
    } kind;
    llvm::Value *V;
    
    TransmitterOperand(Kind kind, llvm::Value *V): kind(kind), V(V) {}

  auto tuple() const {
    return std::make_tuple(kind, V);
  }

  bool operator<(const TransmitterOperand& o) const {
    return tuple() < o.tuple();
  }
    
    llvm::Instruction *I() const {
        return llvm::dyn_cast_or_null<llvm::Instruction>(V);
    }
};

template <class OutputIt>
OutputIt get_transmitter_sensitive_operands(llvm::Instruction *I,
                                            OutputIt out) {
    if (llvm::isa<llvm::LoadInst, llvm::StoreInst>(I)) {
        *out++ = TransmitterOperand(TransmitterOperand::TRUE, llvm::getPointerOperand(I));
    }
    if (llvm::StoreInst *SI = llvm::dyn_cast<llvm::StoreInst>(I)) {
        if (has_incoming_addr(SI->getPointerOperand())) {
            *out++ = TransmitterOperand(TransmitterOperand::PSEUDO, SI->getValueOperand());
        }
    }
    if (llvm::BranchInst *BI = llvm::dyn_cast<llvm::BranchInst>(I)) {
        if (BI->isConditional()) {
            *out++ = TransmitterOperand(TransmitterOperand::TRUE, BI->getCondition());
        }
    }
    if (llvm::SwitchInst *SI = llvm::dyn_cast<llvm::SwitchInst>(I)) {
        *out++ = TransmitterOperand(TransmitterOperand::TRUE, SI->getCondition());
    }       
    if (llvm::CallBase *C = llvm::dyn_cast<llvm::CallBase>(I)) {
        *out++ = TransmitterOperand(TransmitterOperand::TRUE, C->getCalledOperand());
        for (llvm::Value *op : C->args()) {
            *out++ = TransmitterOperand(TransmitterOperand::PSEUDO, op);
        }
    }
    if (llvm::ReturnInst *RI = llvm::dyn_cast<llvm::ReturnInst>(I)) {
        if (llvm::Value *RV = RI->getReturnValue()) {
            *out++ = TransmitterOperand(TransmitterOperand::PSEUDO, RV);
        }
    }
    return out;
}

std::set<TransmitterOperand> get_transmitter_sensitive_operands(llvm::Instruction *I);

