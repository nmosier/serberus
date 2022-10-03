#include <set>
#include <queue>

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Value.h>

#include "clou/analysis/LeakAnalysis.h"
#include "clou/analysis/SpeculativeTaintAnalysis.h"
#include "clou/Transmitter.h"
#include "clou/Mitigation.h"
#include "clou/util.h"

namespace clou {
  using ISet = std::set<llvm::Instruction *>;
  using VSet = std::set<llvm::Value *>;
  using IQueue = std::queue<llvm::Instruction *>;
  
  namespace {
    
    struct NoSpillPublic final : public llvm::FunctionPass {
      static inline char ID = 0;
      NoSpillPublic(): llvm::FunctionPass(ID) {}

      void getAnalysisUsage(llvm::AnalysisUsage& AU) const override {
	AU.addRequired<LeakAnalysis>();
	AU.addRequired<SpeculativeTaint>();
	AU.addPreserved<LeakAnalysis>();
	AU.addPreserved<SpeculativeTaint>();
	AU.setPreservesCFG();
      }

      bool runOnFunction(llvm::Function& F) override {
	auto& LA = getAnalysis<LeakAnalysis>();
	auto& ST = getAnalysis<SpeculativeTaint>();

	bool changed = false;
	for (llvm::Instruction& I : util::nonvoid_instructions(F)) {
	  if (LA.mayLeak(&I) && !ST.secret(&I)) {
	    I.setMetadata("clou.nospill", llvm::MDNode::get(I.getContext(), {}));
	    changed = true;
	  }
	}

	return changed;
      }
    };

    llvm::RegisterPass<NoSpillPublic> X {"clou-nospill-public", "Clou's No Spill Public Pass"};
    util::RegisterClangPass<NoSpillPublic> Y;
  }
}
