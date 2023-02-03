#include "clou/MinCutGreedy2.h"

#include <llvm/ADT/STLExtras.h>

namespace clou {

  void MinCutGreedy2::run(const std::vector<llvm::SparseBitVector>& G) {
    // Deduplicate sts
    {
      llvm::sort(sts);
      auto it = llvm::unique(sts);
      sts.resize(it - sts.begin());
    }

    llvm::errs() << "min-cut on " << getNodes().size() << " nodes\n";

    unsigned i = 0;
    while (true) {
      ++i;
      llvm::errs() << "\titeration " << i << "\n";

      auto reaching_sts = computeReaching();

      float maxw = 0;
      Edge maxe;
      for (const auto& [e, cost] : getEdges()) {
	const unsigned paths = reaching_sts[e]
      }
    }
  }
  
}
