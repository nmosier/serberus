#include "SpeculativeTaint2.h"

#include <map>
#include <set>

#include <llvm/IR/Function.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicsX86.h>

#include "util.h"
#include "Mitigation.h"

namespace clou {

  char SpeculativeTaint::ID = 0;
  SpeculativeTaint::SpeculativeTaint(): llvm::FunctionPass(ID) {}

  void SpeculativeTaint::getAnalysisUsage(llvm::AnalysisUsage& AU) const {
    AU.addRequired<llvm::AAResultsWrapperPass>();
    AU.setPreservesAll();    
  }

  bool SpeculativeTaint::runOnFunction(llvm::Function& F) {
    auto& AA = getAnalysis<llvm::AAResultsWrapperPass>().getAAResults();
    llvm::DominatorTree DT(F);
    llvm::LoopInfo LI(DT);

    std::map<llvm::StoreInst *, ISet> mem, mem_bak;
    TaintMap taints_bak;
    taints.clear();
    do {
      taints_bak = taints;
      mem_bak = mem;

      for (llvm::Instruction& I : llvm::instructions(F)) {
	  
	if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(&I)) {
	  if (has_incoming_addr(LI->getPointerOperand())) {
	    // address dependency is always tainted, unless the result is known to be a secret
	    if (!is_nonspeculative_secret(LI)) {
	      taints[LI].insert(LI);
	    }
	  } else {
	    // check if it must overlap with a public store
	    for (const auto& [SI, origins] : mem) {
	      if (AA.alias(SI->getPointerOperand(), LI->getPointerOperand()) != llvm::AliasResult::NoAlias) {
		taints[LI].insert(origins.begin(), origins.end());
	      }
	    }
	  }
	  continue;
	}

	if (llvm::StoreInst *SI = llvm::dyn_cast<llvm::StoreInst>(&I)) {
	  if (auto *value_I = llvm::dyn_cast<llvm::Instruction>(SI->getValueOperand())) {
	    const auto& orgs = taints[value_I];
	    mem[SI].insert(orgs.begin(), orgs.end());
	  }
	  continue;
	}

	if (llvm::IntrinsicInst *II = llvm::dyn_cast<llvm::IntrinsicInst>(&I)) {
	  if (!II->getType()->isVoidTy() && !II->isAssumeLikeIntrinsic()) {
	    switch (II->getIntrinsicID()) {
	    case llvm::Intrinsic::vector_reduce_and:
	    case llvm::Intrinsic::vector_reduce_or:
	    case llvm::Intrinsic::fshl:
	    case llvm::Intrinsic::umax:
	    case llvm::Intrinsic::umin:
	    case llvm::Intrinsic::ctpop:
	    case llvm::Intrinsic::x86_aesni_aeskeygenassist:
	    case llvm::Intrinsic::x86_aesni_aesenc:
	    case llvm::Intrinsic::x86_aesni_aesenclast:
	    case llvm::Intrinsic::bswap:
	    case llvm::Intrinsic::x86_pclmulqdq:
	    case llvm::Intrinsic::x86_rdrand_32:
	      // Passthrough
	      for (llvm::Value *arg_V : II->args()) {
		if (llvm::Instruction *arg_I = llvm::dyn_cast<llvm::Instruction>(arg_V)) {
		  llvm::copy(taints[arg_I], std::inserter(taints[II], taints[II].end()));
		}
	      }
	      break;
	      
	    default:
	      warn_unhandled_intrinsic(II);
	    }
	  }
	}

	if (llvm::isa<llvm::CallBase>(&I)) {
	  // Calls never return speculatively tainted values by assumption. We must uphold this.
	  continue;
	}

	if (llvm::isa<MitigationInst>(&I)) {
	  continue;
	}

	if (!I.getType()->isVoidTy()) {
	  // taint if any inputs are tainted
	  auto& out = taints[&I];
	  for (llvm::Value *op_V : I.operands()) {
	    if (auto *op_I = llvm::dyn_cast<llvm::Instruction>(op_V)) {
	      const auto& in = taints[op_I];
	      out.insert(in.begin(), in.end());
	    }
	  }
	  continue;
	}
	  
      }
    } while (taints != taints_bak || mem != mem_bak);

    return false;
  }

  bool SpeculativeTaint::secret(llvm::Value *V) {
    if (auto *I = llvm::dyn_cast<llvm::Instruction>(V)) {
      return taints.contains(I);
    } else {
      return false;
    }
  }

  void SpeculativeTaint::print(llvm::raw_ostream& os, const llvm::Module *) const {
    // For now, just print short summary.
    os << "Tainted instructions:\n";
    for (const auto& [I, _] : taints) {
      os << *I << "\n";
    }
  }

  llvm::RegisterPass<SpeculativeTaint> X {"clou-speculative-taint", "Clou's Speculative Taint Analysis Pass", true, true};
  util::RegisterClangPass<SpeculativeTaint> Y;
  
}
