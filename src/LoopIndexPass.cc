#include <vector>

#include <llvm/Analysis/LoopPass.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/LegacyPassManager.h>

#include "util.h"

namespace clou {
  namespace {

    struct LoopIndexPass final : public llvm::LoopPass {
      static inline char ID = 0;
      LoopIndexPass(): llvm::LoopPass(ID) {}

      struct Info {
	llvm::PHINode *phi;
	llvm::Value *init_value;
	llvm::Value *loop_value;
      };

      static bool getBackedgeCond(llvm::Loop *L) {
	llvm::BranchInst *BI = llvm::cast<llvm::BranchInst>(L->getLoopLatch()->getTerminator());
	const unsigned true_idx = 2;
	const unsigned false_idx = 1;
	if (BI->getOperand(true_idx) == L->getHeader()) {
	  return true;
	} else if (BI->getOperand(false_idx) == L->getHeader()) {
	  return false;
	} else {
	  std::abort();
	}
      }

      static llvm::Instruction *getFirstNonPhi(llvm::BasicBlock& B) {
	for (llvm::Instruction& I : B) {
	  if (!llvm::isa<llvm::PHINode>(&I)) {
	    return &I;
	  }
	}
	std::abort();
      }

      static llvm::PHINode *createCondPHI(llvm::Loop *L, bool backedge_cond) {
	llvm::LLVMContext& ctx = L->getHeader()->getContext();
	llvm::Type *i1ty = llvm::IntegerType::get(ctx, 1);
	llvm::IRBuilder<> IRB (&L->getHeader()->front());
	llvm::PHINode *phi = IRB.CreatePHI(i1ty, 2);
	phi->addIncoming(llvm::ConstantInt::getBool(ctx, backedge_cond), L->getLoopPredecessor());
	phi->addIncoming(L->getLatchCmpInst(), L->getLoopLatch());
	return phi;
      }

      bool runOnLoop(llvm::Loop *L, llvm::LPPassManager&) {
	if (!(L->getLoopLatch() && L->getLoopPredecessor() && llvm::isa<llvm::BranchInst>(L->getLoopLatch()->getTerminator()))) {
	  return false;
	}
	
	// find all phi variables
	std::vector<Info> infos;
	for (llvm::Instruction& I : *L->getHeader()) {
	  if (auto *phi = llvm::dyn_cast<llvm::PHINode>(&I)) {
	    // Our approach only works if there's only two inputs -- one from latch, one from unique entering block (i.e., predecessor)
	    llvm::Value *init_value = nullptr;
	    llvm::Value *loop_value = nullptr;
	    for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
	      llvm::Value *V = phi->getIncomingValue(i);
	      llvm::BasicBlock *B = phi->getIncomingBlock(i);
	      if (B == L->getLoopPredecessor()) {
		init_value = V;
	      } else if (B == L->getLoopLatch()) {
		loop_value = V;
	      }
	    }
	    if (init_value && loop_value) {
	      infos.push_back({phi, init_value, loop_value});
	    }
	  }
	}

	if (infos.empty()) {
	  return false;
	}

	// check if phi nodes are dependent
	for (const Info& info : infos) {
	  const auto *phi = info.phi;
	  if (std::any_of(phi->use_begin(), phi->use_end(), [&] (const llvm::Use& use) {
	    if (const auto *phi_user = llvm::dyn_cast<llvm::PHINode>(use.getUser())) {
	      return phi_user->getParent() == L->getHeader();
	    } else {
	      return false;
	    }
	  })) {
	    llvm::errs() << "phi nodes are dependent\n";
	    return false; // TODO: don't just give up, but instead introduce new basic blocks?
	  }
	}

	const bool backedge_cond = getBackedgeCond(L);

	llvm::PHINode *cond_phi = createCondPHI(L, backedge_cond);

	// get first non-phi node
	llvm::Instruction *nonphi = getFirstNonPhi(*L->getHeader());
	for (const Info& info : infos) {
	  llvm::IRBuilder<> IRB (nonphi);
	  llvm::Value *TV = backedge_cond ? info.phi : info.init_value;
	  llvm::Value *FV = backedge_cond ? info.init_value : info.phi;
	  llvm::Value *select = IRB.CreateSelect(cond_phi, TV, FV);
	  info.phi->replaceUsesWithIf(select, [&] (llvm::Use& U) {
	    return U.getUser() != select;
	  });
	}
	
	return true;
      }
    };

    llvm::RegisterPass<LoopIndexPass> X {
      "loop-index-pass", "Loop Index Pass"
    };

    util::RegisterClangPass<LoopIndexPass> Y {
      llvm::PassManagerBuilder::EP_OptimizerLast,
      llvm::PassManagerBuilder::EP_EnabledOnOptLevel0,
    };
  }
}
