#include "transmitter.h"

std::set<llvm::Value *>
get_transmitter_sensitive_operands(llvm::Instruction *I) {
  std::set<llvm::Value *> set;
  get_transmitter_sensitive_operands(I, std::inserter(set, set.end()));
  return set;
}
