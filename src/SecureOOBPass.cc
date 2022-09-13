#include <set>
#include <map>

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/Dominators.h>

#include "util.h"
#include "Transmitter.h"
#include "Mitigation.h"

namespace clou {
  namespace {

    using ISet = std::set<llvm::Instruction *>;

    class Partition {
    public:
      ISet insts;

      llvm::Function *getFunction() const {
	if (insts.empty()) {
	  return nullptr;
	} else {
	  return (*insts.begin())->getFunction();
	}
      }

      bool contains(llvm::Loop *L) const {
	for (auto *B : L->blocks()) {
	  for (auto& I : *B) {
	    if (!insts.contains(&I)) {
	      return false;
	    }
	  }
	}
	return true;
      }

      using Edge = std::pair<llvm::Instruction *, llvm::Instruction *>;

      template <class OutputIt>
      OutputIt getExitEdges(OutputIt out) const {
	for (auto *I : insts) {
	  for (auto *succ : llvm::successors_inst(I)) {
	    if (!insts.contains(succ)) {
	      *out++ = {I, succ};
	    }
	  }
	}
	return out;
      }

      std::vector<Edge> getExitEdges() const {
	std::vector<Edge> edges;
	getExitEdges(std::back_inserter(edges));
	return edges;
      }

      template <class OutputIt>
      OutputIt getExits(OutputIt out) const {
	for (auto *I : insts) {
	  for (auto *succ : llvm::successors_inst(I)) {
	    if (!insts.contains(succ)) {
	      *out++ = succ; 
	    }
	  }
	}
	return out;
      }

      ISet getExits() const {
	ISet exits;
	getExits(std::inserter(exits, exits.end()));
	return exits;
      }

      size_t getNumExits() const {
	return getExits().size();
      }

      template <class OutputIt>
      OutputIt getEntryEdges(OutputIt out) const {
	for (auto *I : insts) {
	  for (auto *pred : llvm::predecessors(I)) {
	    if (!insts.contains(pred)) {
	      *out++ = {pred, I};
	    }
	  }
	}
	return out;
      }

      std::vector<Edge> getEntryEdges() const {
	std::vector<Edge> edges;
	getEntryEdges(std::back_inserter(edges));
	return edges;
      }

      template <class OutputIt>
      OutputIt getEnterings(OutputIt out) const {
	for (auto *I : insts) {
	  for (auto *pred : llvm::predecessors(I)) {
	    if (!insts.contains(pred)) {
	      *out++ = pred;
	    }
	  }
	}
	return out;
      }

      ISet getEnterings() const {
	ISet enterings;
	getEnterings(std::inserter(enterings, enterings.end()));
	return enterings;
      }

      size_t getNumEnterings() const {
	return getEnterings().size();
      }

    private:
      static llvm::BasicBlock *SplitEdge(llvm::Instruction *I_src, llvm::Instruction *I_dst, llvm::LoopInfo *LI) {
	auto *B_src = I_src->getParent();
	assert(&B_src->back() == I_src);
	auto *B_dst = I_dst->getParent();
	assert(&B_dst->front() == I_dst);
	return llvm::SplitEdge(B_src, B_dst, nullptr, LI);
      }

    public:
      /**
       * To make a partition canonical, we need to ensure that (i) all exits are dedicated and (ii) all entries are preheaders
       * (in loop terminology).
       */
      bool makeCanonical(llvm::LoopInfo *LI) {
	bool changed = false;
	
	for (const auto& [entering, entry] : getEntryEdges()) {
	  if (llvm::successors_inst(entering).size() > 1) {
	    SplitEdge(entering, entry, LI);
	    changed = true;
	  }
	}

	for (const auto& [exit, exiting] : getExitEdges()) {
	  if (llvm::predecessors(exiting).size() > 1) {
	    SplitEdge(exit, exiting, LI);
	    changed = true;
	  }
	}

	return changed;
      }
    };

    struct SecureOOBPass final : public llvm::FunctionPass {
      static inline char ID = 0;
      SecureOOBPass(): llvm::FunctionPass(ID) {}

      static bool ignoreCall(const llvm::CallBase *C) {
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

      static void analyzeTaints(llvm::Function& F, ISet& taints) {
	ISet taints_bak;
	do {
	  taints_bak = taints;

	  for (llvm::BasicBlock& B : F) {
	    for (llvm::Instruction& I : B) {
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
      }

      static void getSeparators(llvm::Function& F, ISet& seps) {
	ISet taints;
	analyzeTaints(F, taints);

	for (auto& B : F) {
	  for (auto& I : B) {
	    // Check if this instruction is a transmitter. If so, mark its tainted operand as a separator.
	    // TODO: Should track taint origins to make this more performant.
	    for (const TransmitterOperand& op : get_transmitter_sensitive_operands(&I)) {
	      if (llvm::Instruction *op_I = llvm::dyn_cast<llvm::Instruction>(op.V)) {
		if (!(llvm::isa<llvm::StoreInst>(&I) && op.kind == TransmitterOperand::PSEUDO)) {
		  if (taints.contains(op_I)) {
		    seps.insert(op_I);
		  }
		}
	      }
	    }

	    // Check if this is a call instruction that we can't handle.
	    if (auto *C = llvm::dyn_cast<llvm::CallBase>(&I)) {
	      if (!ignoreCall(C)) {
		seps.insert(C);
	      }
	    }
	  }
	}
      }

      /* Overall Approach:
       * First, partition into a set of instruction regions.
       * For each region, do custom taint analysis.
       * First, do a custom taint analysis, assuming every load returns as speculative secret.
       * Second, partition
       */

      static void partitionFunction(llvm::Function& F, const ISet& seps, std::vector<Partition>& parts) {
	// Flood-fill
	int num_parts = 0;
	std::map<llvm::Instruction *, int> seen;
	for (llvm::BasicBlock& B : F) {
	  for (llvm::Instruction& I : B) {
	    if (!seen.contains(&I)) {
	      std::queue<llvm::Instruction *> todo;
	      todo.push(&I);
	      while (!todo.empty()) {
		auto *I = todo.front();
		todo.pop();
		if (seen.emplace(I, num_parts).second) {
		  for (auto *pred : llvm::predecessors(I)) {
		    if (!seps.contains(pred)) {
		      todo.push(pred);
		    }
		  }
		  for (auto *succ : llvm::successors_inst(I)) {
		    if (!seps.contains(succ)) {
		      todo.push(succ);
		    }
		  }
		}
	      }
	      ++num_parts;
	    }
	  }
	}

	parts.resize(num_parts);
	for (const auto& [I, part] : seen) {
	  parts[part].insts.insert(I);
	}
      }

      void mitigatePartition(Partition& part, llvm::LoopInfo *LI) const {
	part.makeCanonical(LI);

	// Mitigate regular entering edges.
	for (auto *entering : part.getEnterings()) {
	  if (!entering->isTerminator()) {
	    entering = entering->getNextNode();
	  }
	  CreateMitigation(entering, "secure-enter");
	}

	// Mitigate regular exiting edges.
	for (auto *exit : part.getExits()) {
	  CreateMitigation(exit, "secure-exit");
	}	

	// Check if first instruction in function
	auto *first_I = &part.getFunction()->getEntryBlock().front();
	if (part.insts.contains(first_I)) {
	  CreateMitigation(first_I, "secure-enter");
	}	
	
	// Mitigate any return instructions
	for (auto *I : part.insts) {
	  if (llvm::isa<llvm::ReturnInst>(I)) {
	    CreateMitigation(I, "secure-exit");
	  }
	}

	// Mark registers as nospill
	// TODO: Should only mark registers that are transitively transmitter operands.
	for (auto *I : part.insts) {
	  auto& ctx = I->getContext();
	  if (!I->getType()->isVoidTy()) {
	    I->setMetadata("clou.nospill", llvm::MDNode::get(ctx, {}));
	  }
	  I->setMetadata("clou.secure", llvm::MDNode::get(ctx, {}));
	}
      }

      bool partitionContainsLoop(const Partition& part, llvm::LoopInfo *LI) const {
	for (auto *L : LI->getLoopsInPreorder()) {
	  if (part.contains(L)) {
	    return true;
	  }
	}
	return false;
      }

      bool shouldMitigatePartition(const Partition& part, llvm::LoopInfo *LI) const {
	if (partitionContainsLoop(part, LI)) {
	  return true;
	} else if (part.insts.size() > 50) {
	  return true;
	} else {
	  return false;
	}
      }

      bool runOnPartition(Partition& part, llvm::LoopInfo *LI) const {
	if (shouldMitigatePartition(part, LI)) {
	  mitigatePartition(part, LI);
	  return true;
	} else {
	  return false;
	}
      }

      bool runOnFunction(llvm::Function& F) override {
	ISet seps;
	getSeparators(F, seps);

	std::vector<Partition> parts;
	partitionFunction(F, seps, parts);

	bool changed = false;
	llvm::DominatorTree DT(F);
	llvm::LoopInfo LI(DT);
	for (Partition& part : parts) {
	  changed |= runOnPartition(part, &LI);
	}
	
	return changed;
      }
      
      
    };

    llvm::RegisterPass<SecureOOBPass> X {"clou-secure-parts", "Clou Secure Partitions Pass"};
    util::RegisterClangPass<SecureOOBPass> Y;
    
  }
}
