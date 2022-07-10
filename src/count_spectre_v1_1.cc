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

#include "util.h"

struct CountSpectreV11 final : public llvm::FunctionPass {
  static inline char ID = 0;
  CountSpectreV11() : llvm::FunctionPass(ID) {}

  unsigned total_stores = 0;
  unsigned special_stores = 0;

  virtual bool runOnFunction(llvm::Function &F) override {
    for (llvm::BasicBlock &B : F) {
      for (llvm::Instruction &I : B) {
        if (llvm::StoreInst *SI = llvm::dyn_cast<llvm::StoreInst>(&I)) {
          llvm::Value *pointer = SI->getPointerOperand();
          llvm::Value *value = SI->getValueOperand();
          if (llvm::Instruction *I_value =
                  llvm::dyn_cast<llvm::Instruction>(value)) {
            if (llvm::MDNode *MD = I_value->getMetadata("taint")) {
              llvm::StringRef label =
                  llvm::cast<llvm::MDString>(MD->getOperand(0).get())
                      ->getString();
              if (label == "specsec" || label == "secret") {
                if (has_incoming_addr(pointer)) {
                  ++special_stores;
                }
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

llvm::RegisterPass<CountSpectreV11> X{"count-spectre-v1-1",
                                      "Count Spectre v1.1 occurences"};

}
