#include "clou/InstructionPass.h"

#include <set>

namespace clou {

  InstructionPass::InstructionPass(char& pid): llvm::FunctionPass(pid) {}

  bool InstructionPass::runOnFunction(llvm::Function& F) {
    std::set<llvm::Instruction *> insts;
    for (llvm::BasicBlock& B : F) {
      for (llvm::Instruction& I : B) {
	insts.insert(&I);
      }
    }

    bool changed = false;
    for (llvm::Instruction *I : insts) {
      changed |= runOnInstruction(*I);
    }

    return changed;
  }
}
