#include "clou/analysis/SpeculativeTaintAnalysis.h"

#include <map>
#include <set>
#include <cassert>

#include <llvm/IR/Function.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicsX86.h>
#include <llvm/Clou/Clou.h>

#include "clou/util.h"
#include "clou/Mitigation.h"

namespace clou {

  char SpeculativeTaint::ID = 0;
  SpeculativeTaint::SpeculativeTaint(): llvm::FunctionPass(ID) {}

  void SpeculativeTaint::getAnalysisUsage(llvm::AnalysisUsage& AU) const {
    AU.addRequired<llvm::AAResultsWrapperPass>();
    AU.setPreservesAll();    
  }

  static bool isDefinitelyNoAlias(llvm::AliasResult AR) {
    switch (AR) {
    case llvm::AliasResult::NoAlias: return true;
    case llvm::AliasResult::MayAlias: return UnsafeAA;
    case llvm::AliasResult::MustAlias: return false;
    default: std::abort();
    }
  }

  bool SpeculativeTaint::runOnFunction(llvm::Function& F) {
    auto& AA = getAnalysis<llvm::AAResultsWrapperPass>().getAAResults();
    llvm::DominatorTree DT(F);
    llvm::LoopInfo LI(DT);

    std::map<llvm::StoreInst *, std::map<llvm::Instruction *, Kind>> mem, mem_bak;
    TaintMap taints_bak;
    taints.clear();
    do {
      taints_bak = taints;
      mem_bak = mem;

      for (llvm::Instruction& I : llvm::instructions(F)) {
	  
	if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(&I)) {
	  if (!util::isConstantAddress(LI->getPointerOperand())) {
	    taints[LI][LI] = ORIGIN;
	  } else {
	    // check if it may overlap with a secret store
	    for (const auto& [SI, origins] : mem) {
	      if (!isDefinitelyNoAlias(AA.alias(SI->getPointerOperand(), LI->getPointerOperand()))) {
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
	    case llvm::Intrinsic::vector_reduce_add:
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
	    case llvm::Intrinsic::smax:
	    case llvm::Intrinsic::smin:
	    case llvm::Intrinsic::abs:
	    case llvm::Intrinsic::umul_with_overflow:
	    case llvm::Intrinsic::bitreverse:
	    case llvm::Intrinsic::cttz:
	    case llvm::Intrinsic::usub_sat:
	    case llvm::Intrinsic::fmuladd:
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

	  continue;
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

      for (auto& [I, sources] : taints) {
	if (!sources.empty() && !sources.contains(I)) {
	  sources[I] = DERIVED;
	}
      }
      
    } while (taints != taints_bak || mem != mem_bak);

    return false;
  }

  bool SpeculativeTaint::secret(llvm::Value *V) {
    assert(V != nullptr);
    if (auto *I = llvm::dyn_cast<llvm::Instruction>(V)) {
      return !taints[I].empty();
    } else {
      return false;
    }
  }

  void SpeculativeTaint::print(llvm::raw_ostream& os, const llvm::Module *) const {
    // For now, just print short summary.
    os << "Tainted instructions:\n";
    for (const auto& [I, sources] : taints) {
      if (!sources.empty()) {
	os << I << " " << *I << "\n";
      }
    }
  }

  llvm::RegisterPass<SpeculativeTaint> X {"clou-speculative-taint", "Clou's Speculative Taint Analysis Pass", true, true};
  util::RegisterClangPass<SpeculativeTaint> Y;
  
}
