#include <vector>
#include <set>

#include <llvm/Pass.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/Transforms/Utils/LoopUtils.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

#include "util.h"
#include "Transmitter.h"
#include "Mitigation.h"
#include "Log.h"

namespace clou {
  namespace {

    struct SecureOOBPass : public llvm::FunctionPass {
      SecureOOBPass(char& ID): llvm::FunctionPass(ID) {}

      bool ignoreCall(const llvm::CallBase *C) const {
	if (auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(C)) {
	  if (llvm::isa<MitigationInst, llvm::DbgInfoIntrinsic>(II) || II->isAssumeLikeIntrinsic()) {
	    return true;
	  } else {
	    switch (II->getIntrinsicID()) {
	    case llvm::Intrinsic::fshr:
	    case llvm::Intrinsic::fshl:
	      return true;
	    default:
	      warn_unhandled_intrinsic(II);
	      return false;
	    }
	  }
	} else {
	  return false;
	}
      }

      /// Check if we can avoid mitigating Spectre v1.1 in this connected set of blocks.
      /// Outputs set of split points.
      template <class BlockRange, class OutputIt>
      OutputIt getSplitPoints(BlockRange blocks, OutputIt out) const {
	// We can't handle any calls in the loop, since OOB stores will violate the callee's assumptions.
	for (llvm::BasicBlock *B : blocks) {
	  for (llvm::Instruction& I : *B) {
	    if (auto *CB = llvm::dyn_cast<llvm::CallBase>(&I)) {
	      if (!ignoreCall(CB)) {
		*out++ = CB;
	      }
	    }
	  }
	}

	// TODO: Might need to add new assumptions here.
	
	return out;
      }

      template <class BlockRange>
      bool checkBlocks(BlockRange blocks) const {
	std::vector<llvm::CallBase *> CBs;
	getSplitPoints(blocks, std::back_inserter(CBs));
	return CBs.empty();
      }

      template <class BlockRange>
      bool propagateTaint(BlockRange blocks) const {
	std::set<llvm::Instruction *> taints, taints_bak; /*!< Tainted registers */
	// NOTE: Don't need to track tainted memory, since we're already assuming that all loads are tainted.

	// Speculative taint propogation, assuming all loads return secrets
	do {
	  taints_bak = taints;

	  for (llvm::BasicBlock *B : blocks) {
	    for (llvm::Instruction& I : *B) {
	      if (I.getType()->isVoidTy()) {
		// ignore
	      } else if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(&I)) {
		/* We assume that all loads are returning secrets, since we're checking if we can avoid mitigating Spectre v1.1
		 * in this loop. */
		taints.insert(LI);
	      } else {
		/* We purposefully assume that all values defined outside the loop are public. Consider two cases:
		 * (i)  Nonspeculative secret: Any of these will never be leaked anyways, by our CT-Programming assumption.
		 * (ii) Speculative secret: We will eliminate these by inserting a fence before the loop (as well as after the loop).
		 */
		const bool tainted = std::any_of(I.op_begin(), I.op_end(), [&] (llvm::Value *V) -> bool {
		  if (llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V)) {
		    return taints.contains(I);
		  } else {
		    return false;
		  }
		});
		if (tainted) {
		  taints.insert(&I);
		}
	      }
	    }
	  }
	} while (taints != taints_bak);

	// Check if any speculative loop secrets are leaked via transmitters
	for (llvm::BasicBlock *B : blocks) {
	  for (llvm::Instruction& I : *B) {
	    for (const TransmitterOperand& op : get_transmitter_sensitive_operands(&I)) {
	      if (llvm::Instruction *op_I = llvm::dyn_cast<llvm::Instruction>(op.V)) {
		if (!(llvm::isa<llvm::StoreInst>(&I) && op.kind == TransmitterOperand::PSEUDO)) {
		  if (taints.contains(op_I)) {
		    // A speculative secret is leaked, so we need to mitigate Spectre v1.1 in this loop.
#if 1
		    llvm::errs() << getPassName() << ": " << I.getFunction()->getName() << ": speculative secret is leaked:\n"
				 << "Transmitter: " << I << "\n"
				 << "Transmitted: " << *op_I << "\n";
#endif
		    return false;
		  }
		}
	      }
	    }
	  }
	}

	// We don't need to mitigate Spectre v1.1 in this loop.
	// A mitgiation pass using this analysis should insert lfences before and after this loop.
	return true;
      }

      /// Returns whether the blocks should be mitigated. It is up to the caller to choose how to mitigate.
      /// For loops, we will place mitigations in the preheader and dedicated exit blocks.
      /// For regions, 
      template <class BlockRange>
      bool runOnBlocks(BlockRange blocks, std::function<bool (llvm::Instruction *)> contains) const {
	if (blocks.empty()) { return false; }
	
	if (checkBlocks(blocks) && propagateTaint(blocks)) {
	  llvm::LLVMContext& ctx = (*blocks.begin())->getContext();
	  llvm::MDNode *md_secure = llvm::MDNode::get(ctx, {llvm::MDString::get(ctx, "clou.secure")});
	  for (llvm::BasicBlock *B : blocks) {
	    for (llvm::Instruction& I : *B) {
	      I.setMetadata("clou.secure", md_secure);
	    }
	  }

	  // Mark all potential transmitter operands as "nospill".
	  for (llvm::BasicBlock *B : blocks) {
	    for (llvm::Instruction& I : *B) {
	      for (const TransmitterOperand& transop : get_transmitter_sensitive_operands(&I)) {
		if (llvm::Instruction *transop_I = llvm::dyn_cast<llvm::Instruction>(transop.V)) {
		  if (contains(transop_I)) {
		    transop_I->setMetadata("clou.nospill", llvm::MDNode::get(ctx, {}));
		  }
		}
	      }
	    }
	  }

	  return true;
	  
	} else {
	  
	  return false;
	  
	}
      }

    };


    struct SecureOOBLoopPass final : public SecureOOBPass {
      static inline char ID = 0;
      SecureOOBLoopPass(): SecureOOBPass(ID) {}

      bool runOnLoop(llvm::Loop *L, llvm::DominatorTree *DT, llvm::LoopInfo *LI) const {
	if (runOnBlocks(L->blocks(), [L] (llvm::Instruction *I) { return L->contains(I); })) {
	  
	  // Add loop preheader, if necessary
	  if (!L->getLoopPreheader()) {
	    llvm::InsertPreheaderForLoop(L, DT, LI, nullptr, false);
	  }

	  // Create dedicated exit block, if necessary
	  llvm::formDedicatedExitBlocks(L, DT, LI, nullptr, false);
	  
	  // Fence before and after loop
	  CreateMitigation(L->getLoopPreheader()->getTerminator(), "loop-entry");
	  llvm::SmallVector<llvm::BasicBlock *, 4> exits;
	  L->getExitBlocks(exits);
	  for (llvm::BasicBlock *exit : exits) {
	    CreateMitigation(&exit->front(), "loop-exit");
	  }
	  
	  return true;
	  
	} else {
	  
	  return false;
	  
	}
      }

      bool runOnLoopRec(llvm::Loop *L, llvm::DominatorTree *DT, llvm::LoopInfo *LI) const {
	if (runOnLoop(L, DT, LI)) {
	  return true;
	} else {
	  bool changed = false;
	  for (llvm::Loop *L : L->getSubLoops()) {
	    changed |= runOnLoopRec(L, DT, LI);
	  }
	  return changed;
	}
      }

      bool runOnFunction(llvm::Function& F) override {
	llvm::DominatorTree DT(F);
	llvm::LoopInfo LI(DT);
	bool changed = false;
	for (llvm::Loop *L : LI) {
	  changed |= runOnLoopRec(L, &DT, &LI);
	}
	return changed;
      }

    };

    struct SecureOOBBlockPass final : public SecureOOBPass {
      static inline char ID = 0;
      SecureOOBBlockPass(): SecureOOBPass(ID) {}

      bool runOnBlock(llvm::BasicBlock *B) const {
	// Check if we've already marked this block as secure.
	if (B->front().getMetadata("clou.secure")) {
	  return false;
	}

	std::vector<llvm::BasicBlock *> blocks = {B};
	std::vector<llvm::CallBase *> splits;
	getSplitPoints(blocks, std::back_inserter(splits));

	// TODO: Magic number 100 should be tunable.
	if (splits.size() >= static_cast<float>(B->size()) / 50.) {
	  llvm::errs() << getPassName() << ": " << B->getParent()->getName() << ": block would require too many splits (" << splits.size() << " >= " << B->size() << " / 50)\n";
	  return false;
	}

	// Split blocks
	// TODO: Should NOT do this before we know whether the subblocks would be secure.
	// Rewrite this when we write a generalized approach.
	for (llvm::CallBase *C : splits) {
	  llvm::BasicBlock *pre = C->getParent();
	  llvm::BasicBlock *middle = llvm::SplitBlock(pre, C);
	  llvm::BasicBlock *post = llvm::SplitBlock(middle, C->getNextNode());
	  blocks.push_back(post);
	}

	bool changed = false;
	for (llvm::BasicBlock *B : blocks) {
	  // TODO: Tune this parameter.
	  if (countNonDebugInstructions(*B) < 50) {
	    continue;
	  }
	  
	  llvm::iterator_range blocks(&B, &B + 1);
	  if (runOnBlocks(blocks, [&] (llvm::Instruction *I) {
	    return I->getParent() == B;
	  })) {
	    // Insert lfences inside basic block.
	    CreateMitigation(&B->front(), "block-entry");
	    CreateMitigation(B->getTerminator(), "block-exit");
	    changed = true;
	  }
	}

	return changed;
      }

      bool runOnFunction(llvm::Function& F) override {
#if 0
	if (F.getName() == "fill_block_with_xor") { std::abort(); }
#elif 0
	if (F.getName() != "fill_block_with_xor") { return false; }
#endif

	std::vector<llvm::BasicBlock *> blocks;
	for (llvm::BasicBlock& B : F) {
	  blocks.push_back(&B);
	}
	
	bool changed = false;
	for (llvm::BasicBlock *B : blocks) {
	  changed |= runOnBlock(B);
	}
	return changed;
      }

#if 0
      bool doInitialization(llvm::Module& M) override {
	std::ofstream ofs(std::string("/cafe/u/nmosier/clouxx/clouxx-passes/build/") + M.getSourceFileName() + ".module.ll");
	llvm::raw_os_ostream os(ofs);
	os << M;
	return false;
      }
#endif
    };

    llvm::RegisterPass<SecureOOBLoopPass> X1 {"clou-secure-loop", "Clou Secure Loop Pass"};
    util::RegisterClangPass<SecureOOBLoopPass> Y1;
    
    llvm::RegisterPass <SecureOOBBlockPass> X2 {"clou-secure-block", "Clou Secure Block Pass"};
    util::RegisterClangPass<SecureOOBBlockPass> Y2;
    
  }

  }
