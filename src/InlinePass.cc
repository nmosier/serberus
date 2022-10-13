#include <map>
#include <set>

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Attributes.h>
#include <llvm/Transforms/Utils/Cloning.h>

#include "clou/util.h"
#include "clou/analysis/SpeculativeTaintAnalysis.h"
#include "clou/analysis/LeakAnalysis.h"
#include "clou/analysis/NonspeculativeTaintAnalysis.h"

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

      using ISet = std::set<llvm::Instruction *>;
      using IQueue = std::queue<llvm::Instruction *>;
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

      llvm::CallBase *getCallToInline(llvm::Function& F, const CBSet& skip) {
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
	while ((CB = getCallToInline(F, skip))) {
	  llvm::InlineFunctionInfo IFI;
	  const auto result = llvm::InlineFunction(*CB, IFI);
	  if (result.isSuccess()) {
	    changed = true;
	  } else {
	    skip.insert(CB);
	  }
	}
	return changed;
      }
    };

    llvm::RegisterPass<InlinePass> X {"clou-inline-hints", "LLVM-SCT's Inlining Pass"};
    util::RegisterClangPass<InlinePass> Y;
  }
}
