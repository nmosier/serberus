#pragma once

#include <set>
#include <map>

#include <llvm/IR/Value.h>
#include <llvm/Pass.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include "clou/containers.h"

namespace clou {

  class NonspeculativeTaint final: public llvm::FunctionPass {
  public:
    static char ID;
    NonspeculativeTaint();
    
  private:
    std::set<llvm::Value *> pub_vals;
    llvm::Function *F;
    
    void getAnalysisUsage(llvm::AnalysisUsage& AU) const override;
    bool runOnFunction(llvm::Function& F) override;
    void print(llvm::raw_ostream& os, const llvm::Module *M) const override;

    void addAllOperands(llvm::User *U);
    
  public:
    bool secret(llvm::Value *V) const;
  };

}
