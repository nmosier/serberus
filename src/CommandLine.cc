#include "CommandLine.h"

namespace clou {

  llvm::cl::opt<std::string> emit_dot("emit-dot", llvm::cl::desc("Clou: emit dot graphs"));

  int verbose = 0;
  namespace {
    llvm::cl::opt<bool> verbose_flag("clou-verbose", llvm::cl::callback([] (const bool& value) {
      if (value)
	++verbose;
      else
	verbose = 0;
    }));
  }

}
