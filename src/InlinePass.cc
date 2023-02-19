#include <map>
#include <set>

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Attributes.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/IR/IntrinsicsX86.h>

#include "clou/util.h"
#include "clou/analysis/SpeculativeTaintAnalysis.h"
#include "clou/analysis/LeakAnalysis.h"
#include "clou/containers.h"
#include "clou/analysis/NonspeculativeTaintAnalysis.h"
#include "clou/Mitigation.h"

namespace clou {
  namespace {
    struct InlinePass final : public llvm::FunctionPass {
      static inline char ID = 0;
      InlinePass(): llvm::FunctionPass(ID) {}

      using CBSet = std::set<llvm::CallBase *>;

      void getAnalysisUsage(llvm::AnalysisUsage& AU) const override {
	AU.addRequired<NonspeculativeTaint>();
	AU.addRequired<SpeculativeTaint>();
	AU.addRequired<LeakAnalysis>();
      }

      llvm::CallBase *handleSecretStore(llvm::StoreInst *SI, const CBSet& skip) {
	// check if we encounter any public loads along the way
	auto& ST = getAnalysis<SpeculativeTaint>();
	auto& LA = getAnalysis<LeakAnalysis>();

	ISet seen;
	IQueue todo;
	todo.push(SI);
	while (!todo.empty()) {
	  llvm::Instruction *I = todo.front();
	  todo.pop();
	  if (seen.insert(I).second) {
	    if (auto *CB = llvm::dyn_cast<llvm::CallBase>(I)) {
	      if (!skip.contains(CB)) {
		return CB;
	      } else if (llvm::isa<llvm::IntrinsicInst>(CB)) {
		// Do nothing
	      } else {
		continue;
	      }
	    }
	    if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(I)) {
	      if (!ST.secret(LI) && LA.mayLeak(LI)) {
		// stop exploring this path
		continue;
	      }
	    }
	    for (auto *succ : llvm::successors_inst(I)) {
	      todo.push(succ);
	    }
	  }
	}
	return nullptr;
      }

      llvm::CallBase *getCallToInline(llvm::Function& F, CBSet& skip) {
	auto& ST = getAnalysis<SpeculativeTaint>();
	auto& NST = getAnalysis<NonspeculativeTaint>();

	for (llvm::Instruction& I : llvm::instructions(F)) {
	  if (llvm::isa<llvm::CallBase>(&I)) {
	    // ignore
	    // TODO: handle intrinsics properly
	  } else if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(&I)) {
	    llvm::Value *V = SI->getValueOperand();
	    if (!util::isSpeculativeInbounds(SI) && (NST.secret(V) || ST.secret(V))) {
	      if (llvm::CallBase *CB = handleSecretStore(SI, skip)) {
		const llvm::Function *callee = util::getCalledFunction(CB);
		if (callee == &F)
		  skip.insert(CB);
		else
		  return CB;
	      }
	    }
	  }
	}

	return nullptr;
      }

      bool runOnFunction(llvm::Function& F) override {
	llvm::CallBase *CB;
	std::set<llvm::CallBase *> skip;
	bool changed = false;
	int i = 0;
	while ((CB = getCallToInline(F, skip))) {
	  llvm::errs() << "\rInlinePass iteration " << ++i;
	  llvm::InlineFunctionInfo IFI;
	  const auto result = llvm::InlineFunction(*CB, IFI);
	  if (result.isSuccess()) {
	    changed = true;
	  } else {
	    skip.insert(CB);
	  }
	}
	llvm::errs() << "\n";

	// We'll also erase any mitigations that have been introduced.
	std::vector<MitigationInst *> mitigations;
	for (MitigationInst& I : util::instructions<MitigationInst>(F))
	  mitigations.push_back(&I);
	changed |= !mitigations.empty();
	for (MitigationInst *I : mitigations)
	  I->eraseFromParent();

	assert(llvm::none_of(llvm::instructions(F), [] (const llvm::Instruction& I) {
	  if (const auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(&I))
	    return II->getIntrinsicID() == llvm::Intrinsic::x86_sse2_lfence;
	  else
	    return false;
	}));
	
	return changed;
      }
    };

    llvm::RegisterPass<InlinePass> X {"clou-inline-hints", "LLVM-SCT's Inlining Pass"};
    util::RegisterClangPass<InlinePass> Y;
  }
}
