#pragma once

#include <queue>
#include <set>
#include <functional>

#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/ADT/iterator.h>
#include <llvm/IR/InstIterator.h>

#include "clou/containers.h"

namespace clou {

  template <class OutputIt, class UnaryPredicate>
  OutputIt copy_if(llvm::Function& F, OutputIt out, UnaryPredicate pred) {
    using PointerIt = llvm::pointer_iterator<llvm::inst_iterator>;
    return std::copy_if(PointerIt(llvm::inst_begin(F)), PointerIt(llvm::inst_end(F)), out, pred);
  }

  ISet reachable_predecessors(llvm::Instruction *root);
  bool forward_frontier(llvm::Instruction *endpoint, std::function<bool (llvm::Instruction *)> pred, ISet& frontier);
  bool reverse_frontier(llvm::Instruction *endpoint, std::function<bool (llvm::Instruction *)> pred, ISet& frontier);

}
