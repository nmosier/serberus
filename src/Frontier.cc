#include "clou/Frontier.h"

#include <algorithm>

#include "clou/util.h"

namespace clou {

  ISet reachable_predecessors(llvm::Instruction *root) {
    ISet seen;
    IQueue todo;
    todo.push(root);
    while (!todo.empty()) {
      auto *I = todo.front();
      todo.pop();
      if (seen.insert(I).second) {
	for (auto *pred : llvm::predecessors(I)) {
	  todo.push(pred);
	}
      }
    }
    return seen;
  }

  namespace {
    template <class NextFunc>
    bool frontier_impl(llvm::Instruction *endpoint, std::function<bool (llvm::Instruction *)> pred, ISet& frontier,
		       NextFunc nexts) {
      auto& F = *endpoint->getFunction();
      const auto subgraph = reachable_predecessors(endpoint);
      ISet in;
      copy_if(F, std::inserter(in, in.end()), [&] (llvm::Instruction *I) -> bool {
	return subgraph.contains(I) && pred(I);
      });

      ISet seen;
      IQueue todo;
      todo.push(&F.getEntryBlock().front());
      bool result = true;
      auto out = std::inserter(frontier, frontier.end());
      while (!todo.empty()) {
	auto *I = todo.front();
	todo.pop();
	if (seen.insert(I).second) {
	  if (I == endpoint) {
	    result = false;
	  } else if (in.contains(I)) {
	    *out++ = I;
	  } else {
	    for (auto *next : nexts(I)) {
	      todo.push(next);
	    }
	  }
	}
      }

      return result;
    }
  }

  bool forward_frontier(llvm::Instruction *endpoint, std::function<bool (llvm::Instruction *)> pred, ISet& frontier) {
    return frontier_impl(endpoint, pred, frontier, &llvm::successors_inst);
  }

  bool reverse_frontier(llvm::Instruction *endpoint, std::function<bool (llvm::Instruction *)> pred, ISet& frontier) {
    return frontier_impl(endpoint, pred, frontier, [] (llvm::Instruction *I) { return llvm::predecessors(I); });
  }
  
}
