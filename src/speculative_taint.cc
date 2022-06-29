#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/Analysis/AliasAnalysis.h>

#include <set>
#include <map>

#include "util.h"

struct SpeculativeAliasAnalysis {
  llvm::AliasAnalysis& AA;

  SpeculativeAliasAnalysis(llvm::AliasAnalysis& AA): AA(AA) {}

  llvm::AliasResult alias(const llvm::Value *store, const llvm::Value *load) const {
    // check if there's an address dependency
    // NOTE: we always assume the stores are performed to the correct, non-speculative address,
    // since we'd fence them otherwise.
    if (has_incoming_addr(load)) {
      return llvm::AliasResult::MayAlias;
    } else {
      return AA.alias(store, load);
    }
  }
};

struct SpeculativeTaint final: public llvm::FunctionPass {
  static inline char ID = 0;

  static inline constexpr const char *speculative_secret_label = "specsec";

  SpeculativeTaint(): llvm::FunctionPass(ID) {}

  virtual void getAnalysisUsage(llvm::AnalysisUsage& AU) const override {
    AU.addRequired<llvm::AAResultsWrapperPass>();
  }

  virtual bool runOnFunction(llvm::Function& F) override {
    // SpeculativeAliasAnalysis SAA {getAnalysis<llvm::AAResultsWrapperPass>(F)};
    llvm::AliasAnalysis& AA = getAnalysis<llvm::AAResultsWrapperPass>().getAAResults();

    /* Add speculative taint to loads in address dependencies */
#if 0
    for (llvm::BasicBlock& B : F) {
      for (llvm::Instruction& I : B) {
	if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(&I)) {
	  /* check for addr dependency */
	  if (has_incoming_addr(LI)) {
	    /* add taint */
	    add_taint(&I);
	  }
	}
      }
    }
#endif

    /* Propogate taint rules */
    propogate_taint(F, AA);

    return true;
  }

  void propogate_taint(llvm::Function& F, llvm::AliasAnalysis& AA) const {
    bool changed;

    /* Approach:
     * Update taint as we go.
     * Maintain map of taint mems.
     * 
     */
    std::set<llvm::Instruction *> taints, taints_bak; // tainted instructions, initially empty
    using Mem = std::set<const llvm::StoreInst *>;
    std::map<llvm::BasicBlock *, Mem> mems_in, mems_out, mems_in_bak; // tainted memory, initially empty
    
    do {
      changed = false;

      for (llvm::BasicBlock& B : F) {
	Mem mem = std::move(mems_in[&B]);

	for (llvm::Instruction& I : B) {
	  
	  if (llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(&I)) {
	    if (has_incoming_addr(LI)) {
	      // address dependency: always tainted
	      taints.insert(LI);
	    } else {
	      // check if it must overlap with a public store
	      std::vector<const llvm::StoreInst *> rfs;
	      llvm::AliasResult alias = llvm::AliasResult::NoAlias;
	      for (const llvm::StoreInst *SI : mem) {
		const llvm::Value *P = SI->getPointerOperand();
		const llvm::AliasResult cur_alias = AA.alias(P, LI);
		alias = std::max(alias, cur_alias);
		if (cur_alias == llvm::AliasResult::MayAlias) {
		  rfs.push_back(SI);
		}
	      }

	      // TODO: need to use rf information, currently do not
	      if (alias != llvm::AliasResult::NoAlias) {
		taints.insert(LI);
	      }
	    }
	  } else if (llvm::StoreInst *SI = llvm::dyn_cast<llvm::StoreInst>(&I)) {

	    // check if value operand is tainted
	    llvm::Value *V = SI->getValueOperand();
	    llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V);
	    bool tainted = I && taints.contains(I);
	    if (tainted) {
	      mem.insert(SI);
	    } else {
	      // remove stores that have been overwritten with public values
	      for (auto it = mem.begin(); it != mem.end(); ++it) {
		if (AA.alias(SI, *it) == llvm::AliasResult::NoAlias) {
		  it = mem.erase(it);
		}
	      }
	    }
	    
	  } else if (!I.getType()->isVoidTy()) {
	    
	    // taint if any of inputs are tainted
	    bool tainted = std::any_of(I.op_begin(), I.op_end(),
				       [&] (llvm::Value *V) -> bool {
					 if (llvm::Instruction *I =
					     llvm::dyn_cast<llvm::Instruction>(V)) {
					   if (taints.contains(I)) {
					     return true;
					   }
					 }
					 return false;
				       });
	    if (tainted) {
	      taints.insert(&I);
	    }

	  }
	}

	mems_out[&B] = std::move(mem);
	
      }

      /* meet */
      for (llvm::BasicBlock& B : F) {
	Mem& mem = mems_in[&B];
	for (llvm::BasicBlock *B_pred : llvm::predecessors(&B)) {
	  Mem& mem_pred = mems_out[B_pred];
	  mem.merge(mem_pred);
	  assert(mem_pred.empty());
	}
      }

      changed = (taints != taints_bak || mems_in != mems_in_bak);

      taints_bak = taints;
      mems_in_bak = mems_in;
      
    } while (changed);

    // commit taint as metadata
    for (llvm::Instruction *I : taints) {
      add_taint(I);
    }
    
  }

  
  static void add_taint(llvm::Instruction *I) {
    llvm::LLVMContext& ctx = I->getContext();
    I->setMetadata("taint", llvm::MDNode::get(ctx, llvm::ArrayRef<llvm::Metadata *> (llvm::MDString::get(I->getContext(), speculative_secret_label))));
  }
};

namespace {
  llvm::RegisterPass<SpeculativeTaint> X {
    "speculative-taint", "Speculative Taint Pass"
  };
}
