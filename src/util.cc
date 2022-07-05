#include <set>
#include <sstream>

#include <llvm/IR/Instructions.h>

#include "util.h"

namespace {

bool has_incoming_addr(const llvm::Value *V,
                       std::set<const llvm::Value *> &seen) {
  if (!seen.insert(V).second) {
    return false;
  }

  if (llvm::isa<llvm::LoadInst, llvm::Argument>(V)) {
    return true;
  } else if (const llvm::Instruction *I =
                 llvm::dyn_cast<llvm::Instruction>(V)) {
    for (const llvm::Value *op : I->operands()) {
      if (has_incoming_addr(op, seen)) {
        return true;
      }
    }
  }

  return false;
}

} // namespace

bool has_incoming_addr(const llvm::Value *V) {
  std::set<const llvm::Value *> seen;
  return has_incoming_addr(V, seen);
}

std::set<llvm::Value *> get_incoming_loads(llvm::Value *I) {
  std::set<llvm::Value *> set;
  get_incoming_loads(I, std::inserter(set, set.end()));
  return set;
}

bool is_speculative_secret(const llvm::Instruction *I) {
  if (llvm::MDNode *MDN = I->getMetadata("taint")) {
    assert(MDN->getNumOperands() == 1);
    llvm::Metadata *M = MDN->getOperand(0);
    return llvm::cast<llvm::MDString>(M)->getString() == "specsec";
  }
  return false;
}

bool is_speculative_secret(const llvm::Value *V) {
  if (const llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V)) {
    return is_speculative_secret(I);
  } else {
    return false;
  }
}

std::ostream &operator<<(std::ostream &os, const llvm::Value &V) {
  std::string s;
  llvm::raw_string_ostream os_(s);
  os_ << V;
  os << s;
  return os;
}

std::set<llvm::Value *>
get_transmitter_sensitive_operands(llvm::Instruction *I) {
  std::set<llvm::Value *> set;
  get_transmitter_sensitive_operands(I, std::inserter(set, set.end()));
  return set;
}

bool is_nonspeculative_secret(const llvm::Instruction *I) {
  if (llvm::MDNode *MDN = I->getMetadata("taint")) {
    assert(MDN->getNumOperands() == 1);
    llvm::Metadata *M = MDN->getOperand(0);
    return llvm::cast<llvm::MDString>(M)->getString() == "secret";
  }
  return false;
}

bool is_nonspeculative_secret(const llvm::Value *V) {
  if (const llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(V)) {
    return is_nonspeculative_secret(I);
  } else {
    return false;
  }
}

namespace llvm {

std::vector<llvm::Instruction *> predecessors(llvm::Instruction *I) {
  std::vector<llvm::Instruction *> res;
  if (llvm::Instruction *pred = I->getPrevNode()) {
    res.push_back(pred);
  } else {
    for (llvm::BasicBlock *B : llvm::predecessors(I->getParent())) {
      res.push_back(&B->back());
    }
  }
  return res;
}

} // namespace llvm
