#pragma once

#include <set>
#include <tuple>

#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicsX86.h>

#include "util.h"

namespace clou {

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

  bool callDoesNotTransmit(const llvm::CallBase *C);

template <class OutputIt>
OutputIt get_transmitter_sensitive_operands(llvm::Instruction *I,
                                            OutputIt out, bool pseudoStoreValues) {
    if (llvm::isa<llvm::LoadInst, llvm::StoreInst>(I)) {
        *out++ = TransmitterOperand(TransmitterOperand::TRUE, llvm::getPointerOperand(I));
    }
    if (llvm::StoreInst *SI = llvm::dyn_cast<llvm::StoreInst>(I)) {
      if (!util::isSpeculativeInbounds(SI) && pseudoStoreValues) {
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
      if (auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(C)) {
	
	if (!II->isAssumeLikeIntrinsic() && !II->getType()->isVoidTy() && II->arg_size() > 0) {
	  std::vector<unsigned> none;
	  std::vector<unsigned> all;
	  for (unsigned i = 0; i < II->arg_size(); ++i) {
	    all.push_back(i);
	  }
	  std::vector<unsigned> leaked_args;
	  
	  switch (II->getIntrinsicID()) {
	  case llvm::Intrinsic::memset:
	    leaked_args = all;
	    break;
	  case llvm::Intrinsic::memcpy:
	    leaked_args = all;
	    break;
	  case llvm::Intrinsic::vector_reduce_and:
	  case llvm::Intrinsic::vector_reduce_or:
	  case llvm::Intrinsic::fshl:
	  case llvm::Intrinsic::ctpop:
	  case llvm::Intrinsic::x86_aesni_aeskeygenassist:
	  case llvm::Intrinsic::x86_aesni_aesenc:
	  case llvm::Intrinsic::x86_aesni_aesenclast:
	  case llvm::Intrinsic::bswap:
	  case llvm::Intrinsic::x86_pclmulqdq:
	  case llvm::Intrinsic::umin:
	  case llvm::Intrinsic::umax:
	    leaked_args = none;
	    break;
	  default:
	    llvm::errs() << "CLOU: warning: unhandled intrinsic: " << llvm::Intrinsic::getBaseName(II->getIntrinsicID()) << "\n";
	    std::abort();
	  }
	  
	  for (unsigned leaked_arg : leaked_args) {
	    *out++ = TransmitterOperand(TransmitterOperand::PSEUDO, II->getArgOperand(leaked_arg));
	  }

	}
	
      } else {
        *out++ = TransmitterOperand(TransmitterOperand::TRUE, C->getCalledOperand());
        for (llvm::Value *op : C->args()) {
	  *out++ = TransmitterOperand(TransmitterOperand::PSEUDO, op);
        }	
      }
    }
    if (llvm::ReturnInst *RI = llvm::dyn_cast<llvm::ReturnInst>(I)) {
        if (llvm::Value *RV = RI->getReturnValue()) {
            *out++ = TransmitterOperand(TransmitterOperand::PSEUDO, RV);
        }
    }
    return out;
}

  std::set<TransmitterOperand> get_transmitter_sensitive_operands(llvm::Instruction *I, bool pseudStoreValues = true);

}
