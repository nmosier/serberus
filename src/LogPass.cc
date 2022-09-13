#include <string>
#include <fstream>

#include <llvm/Pass.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/raw_os_ostream.h>

#include "util.h"

namespace clou {
  namespace {

    llvm::cl::opt<std::string> LogDir {"clou-log-dir"};

    struct LogPass final : public llvm::FunctionPass {
      static inline char ID = 0;
      LogPass(): llvm::FunctionPass(ID) {}

      bool runOnFunction(llvm::Function& F) override {
	if (!LogDir.getValue().empty()) {
	  std::string s;
	  llvm::raw_string_ostream ss(s);
	  ss << LogDir.getValue() << "/" << F.getName() << ".ll";
	  std::ofstream ofs(s);
	  llvm::raw_os_ostream os(ofs);
	  os << F << "\n";
	}
	return false;
      }
    };

    llvm::RegisterPass<LogPass> X {"log-pass", "Clou Log Pass"};
    util::RegisterClangPass<LogPass> Y;
  }
}
