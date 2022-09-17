#pragma once

#include <utility>
#include <vector>
#include <set>
#include <map>

#include <llvm/Pass.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Analysis/AliasAnalysis.h>

namespace clou {

  class AllocaInitPass final : public llvm::FunctionPass {
  public:
    using ISet = std::set<llvm::Instruction *>;
    
    static char ID;
    AllocaInitPass();

    void getAnalysisUsage(llvm::AnalysisUsage& AU) const override;
    bool runOnFunction(llvm::Function& F) override;

    struct Result {
      ISet stores;
      ISet loads;
    };

    std::map<llvm::AllocaInst *, Result> results;
    
    
  private:
    void getAccessSets(llvm::Function& F, llvm::AliasAnalysis& AA);
    static ISet pruneAccesses(llvm::Function& F, const ISet& in);
  };
  
}
