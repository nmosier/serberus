#include "transmitter.h"

std::set<TransmitterOperand>
get_transmitter_sensitive_operands(llvm::Instruction *I) {
  std::set<TransmitterOperand> set;
  get_transmitter_sensitive_operands(I, std::inserter(set, set.end()));
  return set;
}
