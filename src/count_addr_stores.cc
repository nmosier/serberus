#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Metadata.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

struct CountAddrStores final : public llvm::FunctionPass {
  static inline char ID = 0;
  CountAddrStores() : llvm::FunctionPass(ID) {}

  unsigned total_stores = 0;
  unsigned special_stores = 0;

  virtual bool runOnFunction(llvm::Function &F) override {

    for (llvm::BasicBlock &B : F) {
      for (llvm::Instruction &I : B) {
        if (llvm::StoreInst *SI = llvm::dyn_cast<llvm::StoreInst>(&I)) {
          if (llvm::Instruction *value_I =
                  llvm::dyn_cast<llvm::Instruction>(SI->getValueOperand())) {
            if (llvm::MDNode *M = value_I->getMetadata("taint")) {
              assert(M->getNumOperands() == 1);
              llvm::MDString *MDS =
                  llvm::cast<llvm::MDString>(M->getOperand(0).get());
              if (MDS->getString() == "specsec") {
                ++special_stores;
              }
            }
          }
          ++total_stores;
        }
      }
    }

    return false;
  }

  virtual bool doFinalization(llvm::Module &) override {
    llvm::errs() << "Total stores: " << total_stores << "\n";
    llvm::errs() << "Special stores: " << special_stores << "\n";
    char buf[16];
    sprintf(buf, "%.2f%%",
            static_cast<float>(special_stores) / total_stores * 100);
    llvm::errs() << "Fraction: " << buf << "\n";
    return false;
  }
};

namespace {

llvm::RegisterPass<CountAddrStores> X{
    "count-addr-stores",
    "Count Stores of Address Dependency Results",
};

}
