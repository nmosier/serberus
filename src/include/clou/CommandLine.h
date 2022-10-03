#pragma once

#include <string>

#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>

namespace clou {

  extern llvm::cl::opt<std::string> emit_dot;
  extern int verbose;

  bool enable_tests(void);
  llvm::raw_ostream& tests(void);
  
}
