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
		for (llvm::Value *V : II->args()) {
		  leaks.insert(V);
		}
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
	    for (llvm::Instruction& store : llvm::instructions(F)) {
	      if (store.mayWriteToMemory()) {
		if (llvm::isa<llvm::CallBase, llvm::FenceInst>(&store)) {
		  // ignore
		} else {
		  const auto AR = AA.alias(util::getPointerOperand(load), util::getPointerOperand(&store));
		  if (!isDefinitelyNoAlias(AR)) {
		    if ([[maybe_unused]] const auto *store_LI = llvm::dyn_cast<llvm::LoadInst>(&store)) {
		      assert(store_LI->isAtomic() || store_LI->isVolatile());
		      // ignore
		    } else {
		      for (llvm::Value *store_V : util::getValueOperands(&store)) {
			leaks.insert(store_V);
		      }
		    }
		  }
		}
	      }
	    }

	  } else if (llvm::isa<llvm::CmpInst, llvm::GetElementPtrInst, llvm::BinaryOperator, llvm::PHINode, llvm::CastInst, llvm::SelectInst, llvm::ExtractValueInst, llvm::ExtractElementInst, llvm::InsertElementInst, llvm::ShuffleVectorInst, llvm::FreezeInst>(I)) {

	    // Leaks all input operands
	    for (llvm::Value *op : I->operands()) {
	      leaks.insert(op);
	    }

	  } else if (llvm::isa<llvm::AllocaInst>(I)) {
	    // ignore
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

  namespace {
    llvm::RegisterPass<LeakAnalysis> X {"clou-leak-analysis", "ClouCC's Leak Analysis"};
  }

}
