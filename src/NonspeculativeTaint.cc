#include "clou/NonspeculativeTaint.h"

#include <llvm/Analysis/DependenceAnalysis.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Operator.h>

#include "clou/util.h"
#include "clou/Transmitter.h"
#include "clou/Mitigation.h"
#include "clou/CommandLine.h"

namespace clou {

  char NonspeculativeTaint::ID = 0;

  NonspeculativeTaint::NonspeculativeTaint(): llvm::FunctionPass(ID) {}

  void NonspeculativeTaint::getAnalysisUsage(llvm::AnalysisUsage& AU) const {
    AU.addRequired<llvm::AAResultsWrapperPass>();
    AU.setPreservesAll();
  }

  bool NonspeculativeTaint::runOnFunction(llvm::Function& F) {
    pub_vals.clear();
    this->F = &F;
    
    llvm::AAResults& AA = getAnalysis<llvm::AAResultsWrapperPass>().getAAResults();

    // Initialize public values with transmitter operands. We'll handle call results in the main loop.
    for (llvm::Instruction& I : llvm::instructions(F)) {
      for (const TransmitterOperand& op : get_transmitter_sensitive_operands(&I, false /* no pseudo-stores */)) {
	pub_vals.insert(op.V);
      }
    }

    // All pointer values are public.
    for (llvm::Instruction& I : llvm::instructions(F)) {
      if (I.getType()->isPointerTy()) {
	pub_vals.insert(&I);
      }
    }

    // Add public non-instruction operands (arguments are handled later for simplicity)
    for (llvm::Instruction& I : llvm::instructions(F)) {
      for (llvm::Value *op_V : I.operands()) {
	if (llvm::isa<llvm::Argument, llvm::BasicBlock, llvm::InlineAsm, llvm::Constant>(op_V)) {
	  pub_vals.insert(op_V);
	}
      }
    }

    VSet pub_vals_bak;
    do {
      pub_vals_bak = pub_vals;

      // Propagate calls.
      for (llvm::CallBase& CB : util::instructions<llvm::CallBase>(F)) {
	if (auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(&CB)) {
	  if (!II->getType()->isVoidTy() && !II->isAssumeLikeIntrinsic()) {
	    const auto id = II->getIntrinsicID();
	    enum class Taint {
	      Passthrough,
	      Invalid,
	    } taint_rule = Taint::Invalid;
	  
	    switch (id) {
	    case llvm::Intrinsic::memset:
	    case llvm::Intrinsic::memcpy:
	    case llvm::Intrinsic::x86_sse2_lfence:
	      taint_rule = Taint::Invalid;
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
	    case llvm::Intrinsic::x86_rdrand_32:
	      taint_rule = Taint::Passthrough;
	      break;

	    default:
	      warn_unhandled_intrinsic(II);
	    }

	    switch (taint_rule) {
	    case Taint::Invalid:
	      assert(II->getType()->isVoidTy());
	      break;
	    case Taint::Passthrough:
	      if (pub_vals.contains(II)) {
		for (llvm::Value *V : II->args()) {
		  pub_vals.insert(V);
		}
	      }
	      break;
	    }
	  }
	} else {
	  // Regular call instruction -- assume it conforms to ClouCC CallingConv.
	  // All arguments are public
	  for (llvm::Value *V : CB.args()) {
	    pub_vals.insert(V);
	  }
	  // Return value is public
	  pub_vals.insert(&CB);
	}
      }

      // Propagate public load to all memory accesses of load.
      for (llvm::LoadInst& src : util::instructions<llvm::LoadInst>(F)) {
	if (pub_vals.contains(&src)) {
	  for (auto& dst : llvm::instructions(F)) {
	    if (llvm::isa<llvm::LoadInst, llvm::StoreInst>(&dst)) {
	      if (AA.isMustAlias(src.getPointerOperand(), llvm::getPointerOperand(&dst))) {
		if (auto *dst_SI = llvm::dyn_cast<llvm::StoreInst>(&dst)) {
		  pub_vals.insert(dst_SI->getValueOperand());
		} else if (auto *dst_LI = llvm::dyn_cast<llvm::LoadInst>(&dst)) {
		  pub_vals.insert(dst_LI);
		} else {
		  unhandled_instruction(dst);
		}
	      }
	    }
	  }
	}
      }

      // Normal untaint propagation
      for (llvm::Instruction& I : util::nonvoid_instructions(F)) {
	if (auto *GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(&I)) {
	  addAllOperands(GEP);
	} else if (llvm::isa<llvm::CmpInst, llvm::CastInst, llvm::BinaryOperator, llvm::SelectInst, llvm::PHINode>(&I)) {
	  if (pub_vals.contains(&I)) {
	    addAllOperands(&I);
	  }
	} else if (llvm::isa<llvm::CallBase, llvm::LoadInst, llvm::AllocaInst>(&I)) {
	  // ignore: already handled
	} else if (llvm::isa<llvm::InsertElementInst, llvm::ShuffleVectorInst, llvm::ExtractElementInst, llvm::ExtractValueInst>(&I)) {
	  // ignore: make more precise later
	} else {
	  unhandled_instruction(I);
	}
      }
      
    } while (pub_vals != pub_vals_bak);

    return false;
  }

  void NonspeculativeTaint::addAllOperands(llvm::User *U) {
    for (llvm::Value *op : U->operands()) {
      pub_vals.insert(op);
    }
  }
  
  void NonspeculativeTaint::print(llvm::raw_ostream& os, const llvm::Module *) const {
    os << "Nonspeculatively Public Values:\n";
    for (const llvm::Value *V : pub_vals) {
      if (!llvm::isa<llvm::BasicBlock, llvm::Function>(V)) {
	os << *V << "\n";
      }
    }
    os << "\n";

    if (enable_tests()) {
      for (auto& I : llvm::instructions(*F)) {
	if (!I.getType()->isVoidTy()) {
	  tests() << (secret(&I) ? "sec" : "pub") << " ";
	  I.printAsOperand(tests(), false);
	  tests() << "\n";
	}
      }
    }
  }

  bool NonspeculativeTaint::secret(llvm::Value *V) const {
    return !pub_vals.contains(V);
  }

  namespace {

    llvm::RegisterPass<NonspeculativeTaint> X {
      "nonspeculative-taint", "Nonspeculative Taint Pass", true, true
    };

    util::RegisterClangPass<NonspeculativeTaint> Y;

  }

}
