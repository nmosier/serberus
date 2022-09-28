#pragma once

#include <set>
#include <map>

#include <llvm/IR/Value.h>
#include <llvm/Pass.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

namespace clou {

  class NonspeculativeTaint final: public llvm::FunctionPass {
  public:
    static char ID;
    NonspeculativeTaint();
    
  private:
    using VSet = std::set<llvm::Value *>;
    VSet pub_vals;
    
    void getAnalysisUsage(llvm::AnalysisUsage& AU) const override;
    bool runOnFunction(llvm::Function& F) override;
    void print(llvm::raw_ostream& os, const llvm::Module *M) const override;
    
  public:
    bool secret(llvm::Value *V) const;
  };

}
