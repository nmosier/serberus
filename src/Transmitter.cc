#include "clou/Transmitter.h"

#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/Support/raw_ostream.h>

#include "clou/Mitigation.h"

namespace clou {

  bool callDoesNotTransmit(const llvm::CallBase *C) {
    if (const auto *II = llvm::dyn_cast<llvm::IntrinsicInst>(C)) {
      if (llvm::isa<MitigationInst, llvm::DbgInfoIntrinsic>(II) || II->isAssumeLikeIntrinsic()) {
	return true;
      } else {
	switch (II->getIntrinsicID()) {
	case llvm::Intrinsic::fshr:
	case llvm::Intrinsic::fshl:
	  return true;
	default:
	  warn_unhandled_intrinsic(II);
	  return false;
	}
      }
    } else {
      return true;
    }
  }

std::set<TransmitterOperand>
get_transmitter_sensitive_operands(llvm::Instruction *I) {
  std::set<TransmitterOperand> set;
  get_transmitter_sensitive_operands(I, std::inserter(set, set.end()));
  return set;
}

}
