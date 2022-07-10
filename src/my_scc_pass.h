#pragma once

#include <llvm/Pass.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/Analysis/CallGraphSCCPass.h>

#include <vector>

class MySCCPass: public llvm::ModulePass {
public:
    MySCCPass(char& ID);
    
    using MySCC = std::vector<llvm::CallGraphNode *>;
    
private:
    virtual bool runOnModule(llvm::Module& M) override final;

public:
    virtual bool runOnSCC(const MySCC& SCC) = 0;
};
