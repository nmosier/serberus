#include "util.h"

#include <set>
#include <sstream>
#include <cassert>
#include <algorithm>

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Use.h>

#include "metadata.h"

namespace clou {

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

bool is_nonconstant_value(const llvm::Value *V) {
  if (llvm::isa<llvm::LoadInst, llvm::Argument, llvm::PHINode>(V)) {
    return true;
  } else if (const auto *I = llvm::dyn_cast<llvm::Instruction>(V)) {
    return std::any_of(I->op_begin(), I->op_end(), is_nonconstant_value);
  } else {
    return false;
  }
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

unsigned instruction_loop_nest_depth(llvm::Instruction *I, const llvm::LoopInfo& LI) {
    if (const llvm::Loop *L = LI[I->getParent()]) {
        return L->getLoopDepth() + 1; // TODO: check
    } else {
        return 0;
    }
}

unsigned instruction_dominator_depth(llvm::Instruction *I, const llvm::DominatorTree& DT) {
    if (auto *node = DT[I->getParent()]) {
        return node->getLevel();
    } else {
        return 0;
    }
}

namespace util {

  namespace {
    llvm::Function *getCalledFunctionRec(llvm::Value *V) {
      if (llvm::Function *F = llvm::dyn_cast<llvm::Function>(V)) {
	return F;
      } else if (llvm::BitCastOperator *BCO = llvm::dyn_cast<llvm::BitCastOperator>(V)) {
	assert(BCO->getNumOperands() == 1);
	return getCalledFunctionRec(BCO->getOperand(0));
      } else {
	unhandled_value(*V);
      }
    }
  }

  llvm::Function *getCalledFunction(const llvm::CallBase *C) {
    return getCalledFunctionRec(C->getCalledOperand());
  }

  namespace {
    bool functionIsDirectCallOnlyRec(const llvm::Use& use, std::set<const llvm::User *>& seen) {
      const llvm::User *user = use.getUser();
      llvm::errs() << "User: " << *user << "\n";
      if (!seen.insert(user).second) {
	return false;
      } else if (llvm::isa<llvm::Operator, llvm::Constant>(user)) {
	return std::all_of(user->use_begin(), user->use_end(), [&] (const llvm::Use& use) {
	  return functionIsDirectCallOnlyRec(use, seen);
	});
      } else if (llvm::isa<llvm::Instruction>(user)) {
	return llvm::isa<llvm::CallBase>(user) && use.getOperandNo() == 0;
      } else {
	unhandled_value(*user);
      }
    }
  }
  
  bool functionIsDirectCallOnly(const llvm::Function& F) {
    if (llvm::Function::isLocalLinkage(F.getLinkage())) {
      std::set<const llvm::User *> seen;
      return std::all_of(F.use_begin(), F.use_end(), [&] (const llvm::Use& use) {
	return functionIsDirectCallOnlyRec(use, seen);
      });
    } else {
      return false;
    }
  }

  namespace {

    // FIXME: This isn't entirely correct...
    bool isSpeculativeInboundsValue(const llvm::Value *V) {
      if (const auto *I = llvm::dyn_cast<llvm::Instruction>(V)) {
	if (llvm::isa<llvm::PHINode>(I) || I->mayReadFromMemory()) {
	  return false;
	} else {
	  return std::all_of(I->op_begin(), I->op_end(), isSpeculativeInboundsValue);
	}
      } else {
	return true;
      }
    }
  }

  bool isSpeculativeInbounds(llvm::StoreInst *SI) {
    if (md::getMetadataFlag(SI, md::speculative_inbounds)) {
      return true;
    } else {
      return isSpeculativeInboundsValue(SI->getPointerOperand());
    }
  }
  
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
