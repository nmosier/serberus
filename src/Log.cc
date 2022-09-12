#include "Log.h"

#include <string>

#include <llvm/Support/CommandLine.h>

namespace clou::impl {

  llvm::cl::opt<bool> ShouldLog("clou-log-ll");
  
}
