#pragma once

#include <set>

#include <llvm/Pass.h>
#include <llvm/IR/Value.h>

namespace clou {

  class NonspeculativeLeakAnalysis final : public llvm::FunctionPass {
  public:
    static char ID;
    NonspeculativeLeakAnalysis();

  private:
    using VSet = std::set<llvm::Value *>;
    VSet leaks;
    llvm::Function *F;

    void getAnalysisUsage(llvm::AnalysisUsage& AU) const override;
    bool runOnFunction(llvm::Function& F) override;
    void print(llvm::raw_ostream& os, const llvm::Module *M) const override;

  public:
    bool mayLeak(const llvm::Value *V) const;
  };
  
}
