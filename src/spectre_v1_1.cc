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
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/DependenceAnalysis.h>
#include <llvm/IR/Type.h>
#include <llvm/Analysis/CallGraphSCCPass.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/BasicAliasAnalysis.h>

#include "nonspeculative_taint.h"
#include "util.h"

namespace {

struct SpectreV11 final: public llvm::FunctionPass {
    static inline char ID = 0;
    SpectreV11(): llvm::FunctionPass(ID) {}
    
    virtual void getAnalysisUsage(llvm::AnalysisUsage& AU) const override {
        AU.addRequired<NonspeculativeTaint>();
    }
    
    unsigned total = 0;
    
    virtual bool runOnFunction(llvm::Function& F) override {
        NonspeculativeTaint& NST = getAnalysis<NonspeculativeTaint>();
        
        // find stores with secret operands
        unsigned count = 0;
        for_each_inst<llvm::StoreInst>(F, [&] (llvm::StoreInst *SI) {
            if (has_incoming_addr(SI->getPointerOperand())) {
                llvm::Value *op_V = SI->getValueOperand();
                if (NST.secret(op_V)) {
                    llvm::IRBuilder<> IRB (SI);
                    IRB.CreateFence(llvm::AtomicOrdering::Acquire);
                    ++count;
                }
            }
        });
        
        llvm::errs() << F.getName() << ": " << count << " fences\n";
        
        total += count;
        
        return false;
    }
    
    virtual void print(llvm::raw_ostream& os, const llvm::Module *M) const override {
        (void) M;
        os << total << " fences total\n";
    }
};

llvm::RegisterPass<SpectreV11> X {
    "spectre-v1.1", "Spectre v1.1 Mitigation Pass"
};

}
