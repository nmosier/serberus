#pragma once

#include <type_traits>

#include <llvm/Support/raw_ostream.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/JSON.h>

namespace clou {

  class Stat {
  public:
    Stat(llvm::json::Object& j, llvm::StringRef key): value(j[key]) {}
  protected:
    llvm::json::Value& value;
  };

  class CountStat: public Stat {
  public:
    CountStat(llvm::json::Object& j, llvm::StringRef key, size_t count = 0): Stat(j, key) {
      value = count;
    }

    CountStat& operator++() {
      return this->operator+=(1);
    }

    CountStat& operator++(int) {
      return this->operator++();
    }

    CountStat& operator+=(size_t i) {
      value = *value.getAsUINT64() + i;
      return *this;
    }
  };
  
}
