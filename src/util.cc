#include "clou/util.h"

#include <cassert>
#include <cstdarg>
#include <cstdio>

#include <set>
#include <sstream>
#include <algorithm>
#include <stack>

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Use.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/IntrinsicsX86.h>

#include "clou/Metadata.h"

namespace clou {

std::ostream &operator<<(std::ostream &os, const llvm::Value &V) {
  std::string s;
  llvm::raw_string_ostream os_(s);
  os_ << V;
  os << s;
  return os;
}

unsigned instruction_loop_nest_depth(llvm::Instruction *I, const llvm::LoopInfo& LI) {
    if (const llvm::Loop *L = LI[I->getParent()]) {
        return L->getLoopDepth() + 1; // TODO: check
    } else {
        return 0;
    }
}

unsigned instruction_dominator_depth(llvm::Instruction *I, const llvm::DominatorTree& DT) {
    if (auto *node = DT[I->getParent()]) {
        return node->getLevel();
    } else {
        return 0;
    }
}

  std::set<llvm::Value *> get_incoming_loads(llvm::Value *V) {
    std::set<llvm::Value *> set;
    get_incoming_loads(V, std::inserter(set, set.end()));
    return set;
  }

namespace util {

  llvm::StringRef linkageTypeToString(llvm::GlobalValue::LinkageTypes linkageType) {
    switch (linkageType) {
        case llvm::GlobalValue::PrivateLinkage:
            return "private";
        case llvm::GlobalValue::InternalLinkage:
            return "internal";
        case llvm::GlobalValue::AvailableExternallyLinkage:
            return "available_externally";
        case llvm::GlobalValue::LinkOnceAnyLinkage:
            return "linkonce_any";
        case llvm::GlobalValue::LinkOnceODRLinkage:
            return "linkonce_odr";
        case llvm::GlobalValue::WeakAnyLinkage:
            return "weak_any";
        case llvm::GlobalValue::WeakODRLinkage:
            return "weak_odr";
        case llvm::GlobalValue::CommonLinkage:
            return "common";
        case llvm::GlobalValue::AppendingLinkage:
            return "appending";
        case llvm::GlobalValue::ExternalLinkage:
            return "external";
        default:
            return "unknown_linkage_type";
    }
}

  bool mayLowerToFunctionCall(const llvm::CallBase& C) {
    if (const auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(&C)) {
      if (II->isAssumeLikeIntrinsic())
	return false;
      switch (II->getIntrinsicID()) {
      case llvm::Intrinsic::fshr:
      case llvm::Intrinsic::fshl:
      case llvm::Intrinsic::x86_aesni_aesenc:
      case llvm::Intrinsic::x86_aesni_aeskeygenassist:
      case llvm::Intrinsic::x86_aesni_aesenclast:
      case llvm::Intrinsic::vector_reduce_and:
      case llvm::Intrinsic::vector_reduce_or:
      case llvm::Intrinsic::umax:
      case llvm::Intrinsic::umin:
      case llvm::Intrinsic::smax:
      case llvm::Intrinsic::smin:
      case llvm::Intrinsic::ctpop:
      case llvm::Intrinsic::bswap:
      case llvm::Intrinsic::x86_pclmulqdq:
      case llvm::Intrinsic::x86_rdrand_32:
      case llvm::Intrinsic::vastart:
      case llvm::Intrinsic::vaend:
      case llvm::Intrinsic::vector_reduce_add:
      case llvm::Intrinsic::abs:
      case llvm::Intrinsic::umul_with_overflow:
      case llvm::Intrinsic::bitreverse:
      case llvm::Intrinsic::cttz:
      case llvm::Intrinsic::usub_sat:
      case llvm::Intrinsic::fmuladd:
      case llvm::Intrinsic::annotation:
      case llvm::Intrinsic::x86_sse2_mfence:
      case llvm::Intrinsic::fabs:
      case llvm::Intrinsic::floor:
      case llvm::Intrinsic::x86_sse2_lfence:
      case llvm::Intrinsic::memcpy_inline:
      case llvm::Intrinsic::experimental_constrained_fcmp:
      case llvm::Intrinsic::experimental_constrained_fsub:
      case llvm::Intrinsic::experimental_constrained_fmul:
      case llvm::Intrinsic::experimental_constrained_fcmps:
      case llvm::Intrinsic::experimental_constrained_sitofp:
      case llvm::Intrinsic::experimental_constrained_uitofp:
      case llvm::Intrinsic::experimental_constrained_fadd:	
      case llvm::Intrinsic::experimental_constrained_fptosi:
      case llvm::Intrinsic::experimental_constrained_fdiv:
      case llvm::Intrinsic::experimental_constrained_fptoui:
      case llvm::Intrinsic::experimental_constrained_fpext:
      case llvm::Intrinsic::experimental_constrained_floor:
      case llvm::Intrinsic::experimental_constrained_ceil:
      case llvm::Intrinsic::vacopy:
      case llvm::Intrinsic::experimental_constrained_fptrunc:
      case llvm::Intrinsic::experimental_constrained_fmuladd:
      case llvm::Intrinsic::masked_load:
      case llvm::Intrinsic::masked_gather:
      case llvm::Intrinsic::stacksave:
      case llvm::Intrinsic::stackrestore:
      case llvm::Intrinsic::masked_store:
      case llvm::Intrinsic::vector_reduce_mul:
      case llvm::Intrinsic::vector_reduce_umax:	    	
      case llvm::Intrinsic::vector_reduce_umin:
      case llvm::Intrinsic::vector_reduce_smax:	    	
      case llvm::Intrinsic::vector_reduce_smin:
      case llvm::Intrinsic::vector_reduce_xor:
      case llvm::Intrinsic::trap:
      case llvm::Intrinsic::eh_typeid_for:
      case llvm::Intrinsic::uadd_with_overflow:
      case llvm::Intrinsic::ctlz:
      case llvm::Intrinsic::experimental_constrained_powi:
      case llvm::Intrinsic::experimental_constrained_trunc:
      case llvm::Intrinsic::experimental_constrained_round:
      case llvm::Intrinsic::prefetch:
      case llvm::Intrinsic::uadd_sat:
	return false;

      case llvm::Intrinsic::memset:
      case llvm::Intrinsic::memcpy:
      case llvm::Intrinsic::memmove:
	return true;

      default:
	warn_unhandled_intrinsic(II);
	return true;
      }
    } else {
      return true;
    }
  }

  namespace {
    bool doesNotRecurseRec(const llvm::Function& F, std::set<const llvm::Function *>& seen) {
      if (!seen.insert(&F).second)
	return false;
      if (F.doesNotRecurse())
	return true;
      if (F.isDeclaration())
	return false;
      return llvm::all_of(llvm::instructions(F), [&] (const llvm::Instruction& I) {
	if (const auto *C = llvm::dyn_cast<llvm::CallBase>(&I)) {
	  if (llvm::isa<llvm::IntrinsicInst>(&I))
	    return true;
	  const auto *CalledF = getCalledFunction(C);
	  if (CalledF == nullptr)
	    return false;
	  if (doesNotRecurseRec(*CalledF, seen))
	    return true;
	  return false;
	}
	return true;
      });
    }
  }

  bool doesNotRecurse(const llvm::Function& F) {
    std::set<const llvm::Function *> seen;
    return doesNotRecurseRec(F, seen);
  }

  namespace {
    llvm::Function *getCalledFunctionRec(llvm::Value *V) {
      if (llvm::Function *F = llvm::dyn_cast<llvm::Function>(V)) {
	return F;
      } else if (llvm::BitCastOperator *BCO = llvm::dyn_cast<llvm::BitCastOperator>(V)) {
	assert(BCO->getNumOperands() == 1);
	return getCalledFunctionRec(BCO->getOperand(0));
      } else if (llvm::isa<llvm::Instruction, llvm::Argument, llvm::InlineAsm>(V)) {
	return nullptr;
      } else {
	unhandled_value(*V);
      }
    }
  }

  llvm::Function *getCalledFunction(const llvm::CallBase *C) {
    return getCalledFunctionRec(C->getCalledOperand());
  }

  template <class OutputIt>
  static void getBaseAddresses(const llvm::Value *V, OutputIt out) {
    if (llvm::isa<llvm::Argument, llvm::LoadInst, llvm::CallBase, llvm::GlobalVariable, llvm::AllocaInst>(V)) {
      *out++ = V;
    } else if (const auto *GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(V)) {
      getBaseAddresses(GEP->getPointerOperand(), out);
    } else if (const auto *GEP = llvm::dyn_cast<llvm::GEPOperator>(V)) {
      getBaseAddresses(GEP->getPointerOperand(), out);
    } else if (const auto *BC = llvm::dyn_cast<llvm::BitCastInst>(V)) {
      getBaseAddresses(BC->getOperand(0), out);
    } else if (const auto *BC = llvm::dyn_cast<llvm::BitCastOperator>(V)) {
      getBaseAddresses(BC->getOperand(0), out);
    } else if (const auto *Sel = llvm::dyn_cast<llvm::SelectInst>(V)) {
      getBaseAddresses(Sel->getTrueValue(), out);
      getBaseAddresses(Sel->getFalseValue(), out);
    } else if (llvm::isa<llvm::IntToPtrInst>(V)) {
      *out++ = V;
    } else if (llvm::isa<llvm::ConcreteOperator<llvm::Operator, llvm::Instruction::IntToPtr>>(V)) {
      *out++ = V;
    } else {
      unhandled_value(*V);
    }
  }

  bool isGlobalAddress(const llvm::Value *V) {
    std::vector<const llvm::Value *> bases;
    getBaseAddresses(V, std::back_inserter(bases));
    return llvm::all_of(bases, [] (const llvm::Value *V) {
      return llvm::isa<llvm::GlobalVariable>(V);
    });
  }

  bool isStackAddress(const llvm::Value *V) {
    std::vector<const llvm::Value *> bases;
    getBaseAddresses(V, std::back_inserter(bases));
    return llvm::all_of(bases, [] (const llvm::Value *V) {
      return llvm::isa<llvm::AllocaInst>(V);
    });
  }

  bool isGlobalAddressStore(const llvm::Instruction *SI) {
    if (const llvm::Value *SA = getPointerOperand(SI))
      return isGlobalAddress(SA);
    else
      return false;
  }

  bool isStackAccess(const llvm::Instruction *I) {
    if (const auto *A = getPointerOperand(I))
      return isStackAddress(A);
    else
      return false;
  }
  
}

}

namespace llvm {

std::vector<llvm::Instruction *> predecessors(llvm::Instruction *I) {
  std::vector<llvm::Instruction *> res;
  if (llvm::Instruction *pred = I->getPrevNode()) {
    res.push_back(pred);
  } else {
    for (llvm::BasicBlock *B : llvm::predecessors(I->getParent())) {
      res.push_back(&B->back());
    }
  }
  return res;
}

  std::vector<llvm::Instruction *> successors_inst(llvm::Instruction *I) {
    if (I->isTerminator()) {
      std::vector<llvm::Instruction *> succs;
      for (auto *B : llvm::successors(I)) {
	succs.push_back(&B->front());
      }
      return succs;
    } else {
      return {I->getNextNode()};
    }
  }

  loop_pred_range predecessors(llvm::Loop *L) {
    std::vector<llvm::BasicBlock *> preds;
    for (llvm::BasicBlock *B : llvm::predecessors(L->getHeader())) {
      if (B != L->getHeader()) {
	preds.push_back(B);
      }
    }
    return preds;
  }
  
  loop_succ_range successors(llvm::Loop *L) {
    loop_succ_range exits;
    L->getExitBlocks(exits);
    return exits;
  }


} // namespace llvm


namespace clou::impl {

  void warn_unhandled_intrinsic_(llvm::Intrinsic::ID id, const char *file, size_t line) {
    llvm::errs() << file << ":" << line << ": unhandled intrinsic: " << llvm::Intrinsic::getBaseName(id) << "\n";
    std::_Exit(EXIT_FAILURE);
  }

  void warn_unhandled_intrinsic_(const llvm::IntrinsicInst *II, const char *file, size_t line) {
    warn_unhandled_intrinsic_(II->getIntrinsicID(), file, line);
  }
  
}

namespace clou {

  size_t countNonDebugInstructions(const llvm::BasicBlock& B) {
    return std::count_if(B.begin(), B.end(), [] (const llvm::Instruction& I) {
      if (const auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(&I)) {
	return !II->isAssumeLikeIntrinsic();
      } else {
	return true;
      }
    });
  }

  void trace(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::fputs("CLOU: ", stderr);
    std::vfprintf(stderr, fmt, ap);
    std::fputs("\n", stderr);
    va_end(ap);
  }

  namespace util {
    namespace impl {
      bool nonvoid_inst_predicate::operator()(const llvm::Instruction& I) const {
	return !I.getType()->isVoidTy();
      }

      nonvoid_inst_iterator make_nonvoid_inst_iterator(llvm::inst_iterator it, llvm::inst_iterator end) {
	return nonvoid_inst_iterator(it, end, nonvoid_inst_predicate());
      }
    }
  
    nonvoid_inst_iterator nonvoid_inst_begin(llvm::Function& F) {
      return impl::make_nonvoid_inst_iterator(llvm::inst_begin(F), llvm::inst_end(F));
    }
  
    nonvoid_inst_iterator nonvoid_inst_end(llvm::Function& F) {
      return impl::make_nonvoid_inst_iterator(llvm::inst_end(F), llvm::inst_end(F));
    }
    llvm::iterator_range<nonvoid_inst_iterator> nonvoid_instructions(llvm::Function& F) {
      return llvm::iterator_range<nonvoid_inst_iterator>(nonvoid_inst_begin(F), nonvoid_inst_end(F));
    }

    llvm::Value *getPointerOperand(llvm::Instruction *I) {
      if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(I)) {
	return SI->getPointerOperand();
      } else if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(I)) {
	return LI->getPointerOperand();
      } else if (auto *RMW = llvm::dyn_cast<llvm::AtomicRMWInst>(I)) {
	return RMW->getPointerOperand();
      } else if (auto *XCHG = llvm::dyn_cast<llvm::AtomicCmpXchgInst>(I)) {
	return XCHG->getPointerOperand();
      } else {
	return nullptr;
      }
    }

    // TODO: Make this exhaustive.
    llvm::SmallVector<llvm::Value *, 3> getAccessOperands(llvm::Instruction *I) {
      if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(I)) {
	return {SI->getValueOperand()};
      } else if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(I)) {
	return {LI};
      } else if (auto *RMW = llvm::dyn_cast<llvm::AtomicRMWInst>(I)) {
	return {RMW->getValOperand(), RMW};
      } else if (auto *XCHG = llvm::dyn_cast<llvm::AtomicCmpXchgInst>(I)) {
	return {XCHG->getCompareOperand(), XCHG->getNewValOperand(), XCHG};
      } else {
	return {};
      }
    }

    llvm::SmallVector<llvm::Value *, 3> getValueOperands(llvm::Instruction *I) {
      if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(I)) {
	return {SI->getValueOperand()};
      } else if (auto *RMW = llvm::dyn_cast<llvm::AtomicRMWInst>(I)) {
	return {RMW->getValOperand()};
      } else if (auto *XCHG = llvm::dyn_cast<llvm::AtomicCmpXchgInst>(I)) {
	return {XCHG->getCompareOperand(), XCHG->getNewValOperand()};
      } else {
	unhandled_instruction(*I);
      }
    }

    llvm::Value *getConditionOperand(llvm::Instruction *I) {
      if (auto *BI = llvm::dyn_cast<llvm::BranchInst>(I)) {
	if (BI->isConditional()) {
	  return BI->getCondition();
	}
      } else if (auto *SI = llvm::dyn_cast<llvm::SwitchInst>(I)) {
	return SI->getCondition();
      }
      return nullptr;
    }


    bool ExtCallBase::classof(const llvm::CallBase *C) {
      return !llvm::isa<llvm::IntrinsicInst>(C);
    }

    bool ExtCallBase::classof(const llvm::Value *V) {
      if (const auto *C = llvm::dyn_cast<llvm::CallBase>(V)) {
	return classof(C);
      } else {
	return false;
      }
    }

    void getFrontierBwd(llvm::Instruction *root, const std::set<llvm::Instruction *>& targets, std::set<llvm::Instruction *>& out) {
      const bool hasArg = llvm::any_of(targets, [] (const llvm::Value *V) { return llvm::isa<llvm::Argument>(V); });
      std::stack<llvm::Instruction *> todo;
      std::set<llvm::Instruction *> seen;
      todo.push(root);
      assert(!targets.contains(root));

      while (!todo.empty()) {
	llvm::Instruction *I = todo.top();
	todo.pop();
	if (!seen.insert(I).second)
	  continue;
	if (I != root && targets.contains(I)) {
	  out.insert(I);
	  continue;
	}
	const auto preds = llvm::predecessors(I);
	if (preds.empty() && hasArg) {
	  out.insert(I);
	  continue;
	}
	for (llvm::Instruction *pred : preds)
	  todo.push(pred);
      }
    }

  }
  
}
