#include <set>
#include <queue>

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Value.h>

#include "NonspeculativeTaint.h"
#include "SpeculativeTaint2.h"
#include "Transmitter.h"
#include "Mitigation.h"
#include "util.h"

namespace clou {
  using ISet = std::set<llvm::Instruction *>;
  using VSet = std::set<llvm::Value *>;
  using IQueue = std::queue<llvm::Instruction *>;
  
  namespace {
    
    struct NoSpillPublic final : public llvm::FunctionPass {
      static inline char ID = 0;
      NoSpillPublic(): llvm::FunctionPass(ID) {}

      void getAnalysisUsage(llvm::AnalysisUsage& AU) const override {
	AU.addRequired<NonspeculativeTaint>();
	AU.addRequired<SpeculativeTaint>();
	AU.setPreservesCFG();
      }

      bool runOnFunction(llvm::Function& F) override {
	auto& NST = getAnalysis<NonspeculativeTaint>();
	auto& ST = getAnalysis<SpeculativeTaint>();
	
	// Get set of public instructions
	ISet public_instructions;
	for (llvm::BasicBlock& B : F) {
	  for (llvm::Instruction& I : B) {
	    if (!NST.secret(&I) && !ST.secret(&I) && !I.getType()->isVoidTy()) {
	      public_instructions.insert(&I);
	    }
	  }
	}

	// Get set of (directory or indirectly) transmitted operands
	IQueue todo;
	for (auto& B : F) {
	  for (auto& I : B) {
	    for (const auto& op : get_transmitter_sensitive_operands(&I, false)) {
	      if (auto *op_I = llvm::dyn_cast<llvm::Instruction>(op.V)) {
		todo.push(op_I);
	      }
	    }
	  }
	}

	ISet seen;
	while (!todo.empty()) {
	  auto *I = todo.front();
	  todo.pop();
	  if (llvm::isa<MitigationInst>(I)) {
	    // skip
	  } else {
	    if (seen.insert(I).second) {
	      for (llvm::Value *op_V : I->operands()) {
		if (auto *op_I = llvm::dyn_cast<llvm::Instruction>(op_V)) {
		  todo.push(op_I);
		}
	      }
	    }
	  }
	}

	ISet public_transmitted_operands;
	for (auto *I : seen) {
	  if (!NST.secret(I) && !ST.secret(I)) {
	    I->setMetadata("clou.nospill", llvm::MDNode::get(I->getContext(), {}));
	    llvm::errs() << getPassName() << ": " << F.getName() << ": clou.nospill: " << *I << "\n";
	  }
	}

	return true;
      }
    };

    llvm::RegisterPass<NoSpillPublic> X {"clou-nospill-public", "Clou's No Spill Public Pass"};
    util::RegisterClangPass<NoSpillPublic> Y;
  }
}
