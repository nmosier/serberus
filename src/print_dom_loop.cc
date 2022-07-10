#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/LoopInfo.h>

#include "util.h"

namespace {

struct PrintDomLoop final: public llvm::FunctionPass {
    static inline char ID = 0;
    PrintDomLoop(): llvm::FunctionPass(ID) {}

    virtual bool runOnFunction(llvm::Function& F) override {
        llvm::DominatorTree DT (F);
        llvm::LoopInfo LI (DT);
        
        for (llvm::BasicBlock& B : F) {
            llvm::errs() << "Basic Block:\n" << B << "\n";
            llvm::errs() << "Dominator Tree: ";
            if (auto *DTN = DT[&B]) {
                llvm::errs() << DTN->getLevel();
            } else {
                llvm::errs() << "(none)";
            }
            llvm::errs() << "\n";
            
            llvm::errs() << "Loop Info: ";
            if (auto *LIN = LI[&B]) {
                llvm::errs() << LIN->getLoopDepth();
            } else {
                llvm::errs() << "(none)";
            }
            llvm::errs() << "\n";
        }
        
        return false;
    }
};

llvm::RegisterPass<PrintDomLoop> X {
    "print-dom-loop",
    "Print Dominator Loop"
};

}

