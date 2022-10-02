#include "CommandLine.h"

#include <llvm/Support/raw_os_ostream.h>

#include <string>
#include <fstream>

namespace clou {

  llvm::cl::opt<std::string> emit_dot("emit-dot", llvm::cl::desc("Clou: emit dot graphs"));

  int verbose = 0;
  namespace {
    llvm::cl::opt<bool> verbose_flag {
      "clou-verbose",
      llvm::cl::desc("verbose output"),
      llvm::cl::callback([] (const bool& value) {
	if (value) {
	  ++verbose;
	} else {
	  verbose = 0;
	}
      }),
      llvm::cl::init(false),
    };
  }

  namespace {
    std::ofstream test_output_file;
    llvm::raw_os_ostream test_output_file_(test_output_file);
    bool enable_tests_ = false;
    llvm::cl::opt<std::string> test_output_filename {
      "clou-test",
      llvm::cl::desc("Filename for ClouCC's test output"),
      llvm::cl::callback([] (const std::string& s) {
	test_output_file.open(s);
	enable_tests_ = true;
      }),
    };
  }

  bool enable_tests(void) {
    return enable_tests_;
  }

  llvm::raw_ostream& tests(void) {
    return test_output_file_;
  }
}
