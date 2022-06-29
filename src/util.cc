#include <llvm/IR/Instructions.h>

#include "util.h"

bool has_incoming_addr(const llvm::Instruction *I) {
    for (const llvm::Value *U : I->operands()) {
      if (const llvm::Instruction *src = llvm::dyn_cast<llvm::Instruction>(U)) {
	if (llvm::isa<llvm::LoadInst>(src) || has_incoming_addr(src)) {
	  return true;
	}
      } else if (llvm::isa<llvm::Argument>(U)) {
	return true;
      }
    }
    return false;
}

bool has_incoming_addr(const llvm::Value *V) {
  if (const llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V)) {
    return has_incoming_addr(V);
  } else {
    return false;
  }
}

