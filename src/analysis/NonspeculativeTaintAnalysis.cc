#include "clou/analysis/NonspeculativeTaintAnalysis.h"

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
#include <llvm/Clou/Clou.h>

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

  // Only return `true` if we're sure that it's a must alias.
  static bool isDefinitelyMustAlias(llvm::AliasResult AR) {
    switch (AR) {
    case llvm::AliasResult::MustAlias: return true;
    case llvm::AliasResult::MayAlias: return UnsafeAA;
    case llvm::AliasResult::NoAlias: return false;
    default: std::abort();
    }
  }

  bool NonspeculativeTaint::runOnFunction(llvm::Function& F) {
    pub_vals.clear();
    this->F = &F;
    
    llvm::AAResults& AA = getAnalysis<llvm::AAResultsWrapperPass>().getAAResults();

    // Initialize public values with transmitter operands. We'll handle call results in the main loop.
    for (llvm::Instruction& I : llvm::instructions(F)) {
      for (const TransmitterOperand& op : get_transmitter_sensitive_operands(&I)) {
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
	      Handled,
	    } taint_rule = Taint::Invalid;
	  
	    switch (id) {
	    case llvm::Intrinsic::memset:
	    case llvm::Intrinsic::memcpy:
	    case llvm::Intrinsic::x86_sse2_lfence:
	      taint_rule = Taint::Invalid;
	      break;

	    case llvm::Intrinsic::vector_reduce_and:
	    case llvm::Intrinsic::vector_reduce_add:	      
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
	    case llvm::Intrinsic::smin:
	    case llvm::Intrinsic::smax:
	    case llvm::Intrinsic::abs:	      
	    case llvm::Intrinsic::x86_rdrand_32:
	    case llvm::Intrinsic::umul_with_overflow:
	    case llvm::Intrinsic::bitreverse:
	    case llvm::Intrinsic::cttz:
	    case llvm::Intrinsic::usub_sat:
	    case llvm::Intrinsic::fmuladd:
	      taint_rule = Taint::Passthrough;
	      break;

	    case llvm::Intrinsic::annotation:
	      if (pub_vals.contains(II)) {
		pub_vals.insert(II->getArgOperand(0));
	      }
	      taint_rule = Taint::Handled;
	      break;

	    default:
	      warn_unhandled_intrinsic(II);
	    }

	    switch (taint_rule) {
	    case Taint::Handled:
	      break;
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

      // Propagate taint through all memory access instructions.
      for (llvm::Instruction& src : llvm::instructions(F)) {
	llvm::Value *src_ptr = util::getPointerOperand(&src);
	const auto src_vals = util::getAccessOperands(&src);
	const auto src_vals_tainted = llvm::any_of(src_vals, [&] (llvm::Value *access) {
	  return pub_vals.contains(access);
	});
	if (src_ptr && src_vals_tainted) {
	  llvm::copy(src_vals, std::inserter(pub_vals, pub_vals.end())); // all source values are tainted
	  for (llvm::Instruction& dst : llvm::instructions(F)) {
	    if (llvm::Value *dst_ptr = util::getPointerOperand(&dst)) {
	      if (isDefinitelyMustAlias(AA.alias(src_ptr, dst_ptr))) {
		llvm::copy(util::getAccessOperands(&dst), std::inserter(pub_vals, pub_vals.end()));
	      }
	    }
	  }
	}
      }

      for (llvm::Instruction& I : util::nonvoid_instructions(F)) {
	if (auto *GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(&I)) {
	  addAllOperands(GEP);
	} else if (llvm::isa<llvm::CmpInst, llvm::CastInst, llvm::BinaryOperator, llvm::SelectInst, llvm::PHINode, llvm::FreezeInst>(&I)) {
	  if (pub_vals.contains(&I)) {
	    addAllOperands(&I);
	  }
	} else if (auto *UI = llvm::dyn_cast<llvm::UnaryOperator>(&I)) {
	  assert(UI->getOpcode() == llvm::UnaryOperator::UnaryOps::FNeg);
	  if (pub_vals.contains(&I)) {
	    addAllOperands(&I);
	  }
	} else if (llvm::isa<llvm::CallBase, llvm::LoadInst, llvm::AllocaInst, llvm::AtomicRMWInst, llvm::AtomicCmpXchgInst>(&I)) {
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

    llvm::RegisterPass<NonspeculativeTaint> X {"clou-nonspeculative-taint-analysis", "Clou's Nonspeculative Taint Analysis"};
    util::RegisterClangPass<NonspeculativeTaint> Y;

  }

}
