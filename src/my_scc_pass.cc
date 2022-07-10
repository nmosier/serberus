#include <llvm/ADT/SCCIterator.h>

#include "my_scc_pass.h"

MySCCPass::MySCCPass(char& ID): llvm::ModulePass(ID) {}

bool MySCCPass::runOnModule(llvm::Module& M) {
    llvm::CallGraph CG (M);
    bool result = false;
    for (llvm::scc_iterator<llvm::CallGraph *> it = llvm::scc_begin(&CG); !it.isAtEnd(); ++it) {
        const auto& ref = *it;
        result |= runOnSCC(ref);
    }
    return result;
}
