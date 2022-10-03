#pragma once

#include <llvm/Pass.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Function.h>

namespace clou {

class InstructionPass: public llvm::FunctionPass {
public:
  InstructionPass(char& pid);
  
  virtual bool runOnInstruction(llvm::Instruction& I) = 0;

  template <typename AnalysisType>
  AnalysisType& getAnalysis(llvm::Instruction& I) {
    return getAnalysis<AnalysisType>(*I.getFunction());
  }
  
private:
  bool runOnFunction(llvm::Function& F) override;
};

}
