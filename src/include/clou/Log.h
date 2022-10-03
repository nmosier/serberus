#pragma once

#include <string>
#include <fstream>

#include <llvm/IR/Function.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/raw_os_ostream.h>

namespace clou {

  namespace impl {
    extern llvm::cl::opt<bool> ShouldLog;
  }

  template <class T>
  class Logger {
  public:
    Logger(const T& value, const std::string& path): path(path), value(value) {
      write("before");
    }

    ~Logger() {
      write("after");
    }

  private:
    std::string path;
    const T& value;

    bool shouldLog() const {
      return impl::ShouldLog.getValue();
    }

    std::string getPath(llvm::StringRef label) const {
      return path + "." + label.str() + ".ll";
    }

    void write(llvm::StringRef label) const {
      if (shouldLog()) {
	std::ofstream ofs(getPath(label));
	llvm::raw_os_ostream os(ofs);
	os << value;
      }
    }
  };

  class FunctionLogger {
  public:
    FunctionLogger(const llvm::Function& F, const std::string& dir, const std::string& passname): logger(F, dir + "/" + F.getName().str() + "." + passname) {}
  private:
    Logger<llvm::Function> logger;
  };

}
