#include "clou/CFG.h"

#include <queue>

#include <llvm/IR/CFG.h>


namespace clou {

  std::set<llvm::Instruction *> getInstructionsReachableFrom(llvm::Instruction *start, bool forward,
							     bool inclusive) {
    std::set<llvm::Instruction *> seen;
    std::queue<llvm::Instruction *> todo;
    todo.push(start);

    while (!todo.empty()) {
      llvm::Instruction *I = todo.front();
      todo.pop();

      if (seen.contains(I))
	continue;

      if (inclusive)
	seen.insert(I);

      // get next nodes
      std::set<llvm::Instruction *> succs;
      if (forward) {
	if (auto *succ = I->getNextNode())
	  succs = {succ};
	else
	  llvm::transform(llvm::successors(I), std::inserter(succs, succs.end()), [] (llvm::BasicBlock *B) { return &B->front(); });
      } else {
	if (auto *pred = I->getPrevNode())
	  succs = {pred};
	else
	  llvm::transform(llvm::predecessors(I->getParent()), std::inserter(succs, succs.end()),
			  [] (llvm::BasicBlock *B) { return &B->back(); });
      }

      if (!inclusive)
	llvm::copy(succs, std::inserter(seen, seen.end()));

      for (llvm::Instruction *succ : succs)
	todo.push(succ);
    }

    return seen;
  }

  std::set<llvm::Instruction *> getInstructionsBetween(llvm::Instruction *start, llvm::Instruction *stop) {
    const auto fwd = getInstructionsReachableFrom(start, true, false);
    const auto bwd = getInstructionsReachableFrom(stop, false, false);
    std::set<llvm::Instruction *> result;
    std::set_intersection(fwd.begin(), fwd.end(), bwd.begin(), bwd.end(), std::inserter(result, result.end()));
    return result;
  }
  
}
