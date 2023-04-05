#include "clou/analysis/LeakAnalysis.h"

#include <cassert>

#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicsX86.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Clou/Clou.h>

#include "clou/Transmitter.h"
#include "clou/CommandLine.h"
#include "clou/containers.h"

namespace clou {

  char LeakAnalysis::ID = 0;
  LeakAnalysis::LeakAnalysis(): llvm::FunctionPass(ID) {}

  bool LeakAnalysis::mayLeak(const llvm::Value *V) const {
    return leaks.contains(const_cast<llvm::Value *>(V));
  }
  
  void LeakAnalysis::getAnalysisUsage(llvm::AnalysisUsage& AU) const {
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

  class AliasingStores {
  public:
    AliasingStores(llvm::AliasAnalysis& AA): AA(AA) {}

    const ISet& getAliasingStores(llvm::Instruction *Load) {
      Map::const_iterator it = aliases.find(Load);
      if (it == aliases.end())
	it = computeAliases(Load);
      return it->second; 
    }
    
  private:
    llvm::AliasAnalysis& AA;
    using Map = std::map<llvm::Instruction *, ISet>;
    Map aliases;

    Map::const_iterator computeAliases(llvm::Instruction *Load) {
      llvm::Function& F = *Load->getParent()->getParent();
      assert(!aliases.contains(Load));
      ISet Stores;
      for (llvm::Instruction& Store : llvm::instructions(F)) {
	if (!Store.mayWriteToMemory())
	  continue;
	if (llvm::isa<llvm::CallBase, llvm::FenceInst>(&Store))
	  continue;
	const auto AR = AA.alias(util::getPointerOperand(Load), util::getPointerOperand(&Store));
	if (isDefinitelyNoAlias(AR))
	  continue;
	Stores.insert(&Store);
      }
      const auto result = aliases.emplace(Load, std::move(Stores));
      assert(result.second && "Alias set already computed for this load!");
      return result.first;
    }
  };

  bool LeakAnalysis::runOnFunction(llvm::Function& F) {
    this->F = &F;
    leaks.clear();
    
    llvm::AliasAnalysis& AA = getAnalysis<llvm::AAResultsWrapperPass>().getAAResults();
    llvm::DataLayout DL (F.getParent());
    
    // Add all true transmitter operands.
    for (llvm::Instruction& I : llvm::instructions(F)) {
      for (const TransmitterOperand& op : get_transmitter_sensitive_operands(&I)) {
	leaks.insert(op.V);
      }
    }

    AliasingStores Aliases(AA);

    VSet leaks_bak;
    do {
      leaks_bak = leaks;

      for (llvm::Value *V : leaks) {
	if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V)) {
	  if (llvm::CallBase *CB = llvm::dyn_cast<llvm::CallBase>(I)) {
	    
	    if (llvm::IntrinsicInst *II = llvm::dyn_cast<llvm::IntrinsicInst>(CB)) {
	      switch (II->getIntrinsicID()) {
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
	      case llvm::Intrinsic::x86_rdrand_32:
	      case llvm::Intrinsic::smax:
	      case llvm::Intrinsic::smin:
	      case llvm::Intrinsic::umul_with_overflow:
	      case llvm::Intrinsic::abs:
	      case llvm::Intrinsic::cttz:
	      case llvm::Intrinsic::usub_sat:
	      case llvm::Intrinsic::fmuladd:
	      case llvm::Intrinsic::fabs:
	      case llvm::Intrinsic::experimental_constrained_fcmp:
	      case llvm::Intrinsic::experimental_constrained_fmul:
	      case llvm::Intrinsic::experimental_constrained_fsub:
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
	      case llvm::Intrinsic::bitreverse:
	      case llvm::Intrinsic::masked_load:
	      case llvm::Intrinsic::masked_gather:
	      case llvm::Intrinsic::experimental_constrained_fptrunc:
	      case llvm::Intrinsic::experimental_constrained_fmuladd:
	      case llvm::Intrinsic::fshr:
	      case llvm::Intrinsic::vector_reduce_mul:
	      case llvm::Intrinsic::vector_reduce_umax:	    		
	      case llvm::Intrinsic::vector_reduce_umin:
	      case llvm::Intrinsic::vector_reduce_smax:	    		
	      case llvm::Intrinsic::vector_reduce_xor:
	      case llvm::Intrinsic::vector_reduce_smin:
	      case llvm::Intrinsic::eh_typeid_for:
	      case llvm::Intrinsic::uadd_with_overflow:
	      case llvm::Intrinsic::ctlz:
	      case llvm::Intrinsic::experimental_constrained_powi:
	      case llvm::Intrinsic::experimental_constrained_trunc:		
	      case llvm::Intrinsic::experimental_constrained_round:
	      case llvm::Intrinsic::uadd_sat:		
		for (llvm::Value *V : II->args()) {
		  leaks.insert(V);
		}
		break;

	      case llvm::Intrinsic::annotation:
		leaks.insert(II->getArgOperand(0));
		break;

	      default:
		warn_unhandled_intrinsic(II);
	      }
	    } else {
	      // All arguments may nonspeculatively leak.
	      for (llvm::Value *arg : CB->args()) {
		leaks.insert(arg); 
	      }
	    }

	  } else if (llvm::isa<llvm::LoadInst, llvm::AtomicRMWInst>(I)) {
	    llvm::Instruction *load = I;

	    if (llvm::isa<llvm::LoadInst>(load)) {
	      // nothing extra to do
	    } else if (auto *load_RMW = llvm::dyn_cast<llvm::AtomicRMWInst>(load)) {
	      // operand is definitely leaked
	      leaks.insert(load_RMW->getValOperand());
	    } else {
	      unhandled_instruction(*load);
	    }
	    
	    // Find all potentially overlapping stores.
	    for (llvm::Instruction *Store : Aliases.getAliasingStores(load)) {
	      if ([[maybe_unused]] const auto *store_LI = llvm::dyn_cast<llvm::LoadInst>(Store))
		assert(store_LI->isAtomic() || store_LI->isVolatile());
	      else
		llvm::copy(util::getValueOperands(Store), std::inserter(leaks, leaks.end()));
	    }

	  } else if (llvm::isa<llvm::CmpInst, llvm::GetElementPtrInst, llvm::BinaryOperator, llvm::PHINode, llvm::CastInst, llvm::SelectInst,
		     llvm::ExtractValueInst, llvm::ExtractElementInst, llvm::InsertElementInst, llvm::ShuffleVectorInst, llvm::FreezeInst,
		     llvm::InsertValueInst>(I)
		     || (llvm::isa<llvm::UnaryOperator>(I) && llvm::cast<llvm::UnaryOperator>(I)->getOpcode() == llvm::UnaryOperator::UnaryOps::FNeg)
		     ) {
	    // Leaks all input operands
	    for (llvm::Value *op : I->operands()) {
	      leaks.insert(op);
	    }

	  } else if (llvm::isa<llvm::AllocaInst>(I)) {
	    // ignore

	  } else if (llvm::isa<llvm::LandingPadInst>(I)) {
	    // unhandled for now

	  } else {
	    unhandled_instruction(I);
	  }
	}
      }
      
    } while (leaks != leaks_bak);
    
    return false;
  }
  
  void LeakAnalysis::print(llvm::raw_ostream& os, const llvm::Module *) const {
    os << "Nonspeculatively Leaked Values:\n";
    for (const llvm::Value *leak : leaks) {
      if (llvm::isa<llvm::Instruction>(leak)) {
	os << *leak;
      } else {
	leak->printAsOperand(os, false);
      }
      os << "\n";
    }
    os << "\n";

    if (enable_tests()) {
      for (const llvm::Value *leak : leaks) {
	tests() << "+ ";
	leak->printAsOperand(tests(), false);
	tests() << "\n";
      }
      for (auto& I : util::nonvoid_instructions(*F)) {
	if (!mayLeak(&I)) {
	  tests() << "- ";
	  I.printAsOperand(tests(), false);
	  tests() << "\n";
	}
      }
    }
  }

  static llvm::RegisterPass<LeakAnalysis> X {"clou-leak-analysis", "ClouCC's Leak Analysis"};

}
