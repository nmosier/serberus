#pragma once

#include <llvm/Pass.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/IRBuilder.h>

namespace clou {

  class FenceReplacementPass : public llvm::FunctionPass {
  public:
    FenceReplacementPass(char& ID): llvm::FunctionPass(ID) {}
    bool runOnFunction(llvm::Function& F) override;

  protected:
    virtual llvm::Instruction *createInst(llvm::IRBuilder<>& IRB) const = 0;
  };

}
