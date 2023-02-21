#pragma once

#include <map>

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

namespace clou {

  class ConstantAddressAnalysis final : public llvm::ModulePass {
  public:
    static inline char ID = 0;
    ConstantAddressAnalysis(): llvm::ModulePass(ID) {}

    bool isConstantAddress(const llvm::Value *V) const;

    std::set<const llvm::Argument *> getConstAddrArgs(const llvm::Function *F) const {
      const auto it = ca_args.find(F);
      if (it == ca_args.end())
	return {};
      else
	return it->second;
    }

  private:
    using ArgumentSet = std::set<const llvm::Argument *>;
    std::map<const llvm::Function *, ArgumentSet> ca_args;

    void getAnalysisUsage(llvm::AnalysisUsage& AU) const override;
    bool runOnModule(llvm::Module& M) override;
  };

}
