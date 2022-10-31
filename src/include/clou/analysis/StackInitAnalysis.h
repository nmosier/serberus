#pragma once

#include <utility>
#include <vector>
#include <set>
#include <map>

#include <llvm/Pass.h>
#include <llvm/IR/Instructions.h>

#include "clou/containers.h"

namespace clou {

  class StackInitAnalysis final : public llvm::FunctionPass {
  public:
    static char ID;
    StackInitAnalysis();

    void getAnalysisUsage(llvm::AnalysisUsage& AU) const override;
    bool runOnFunction(llvm::Function& F) override;
    void print(llvm::raw_ostream& os, const llvm::Module *) const override;

    struct Result {
      ISet stores;
      ISet loads;
    };

    using Results = std::map<llvm::AllocaInst *, Result>;
    Results results;
    
  private:
  };
  
}
