#include "MinCutBase.h"

#include <llvm/Support/CommandLine.h>

namespace clou {

  bool optimized_min_cut;
  static llvm::cl::opt<bool, true> optimized_min_cut_flag {
    "clou-min-cut-opt",
    llvm::cl::desc("Optimize min-cut algorithm"),
    llvm::cl::location(optimized_min_cut),
    llvm::cl::init(true),
  };

}
