#pragma once

#include <type_traits>

#include <llvm/Support/raw_ostream.h>
#include <llvm/ADT/StringRef.h>

namespace clou {

  template <class Derived>
  class Stat {
    // static_assert(std::is_base_of<Stat, Derived>()(), "");
  public:
    Stat(llvm::raw_ostream& os): os(os) {}
    ~Stat() {
      os << static_cast<Derived *>(this)->key() << ": ";
      static_cast<Derived *>(this)->print_value();
      os << "\n";
    }

  protected:
    llvm::raw_ostream& os;
  };

  class CountStat: public Stat<CountStat> {
  public:
    llvm::StringRef key_;
    size_t count;
    
    CountStat(llvm::StringRef key, llvm::raw_ostream& os): Stat(os), key_(key), count(0) {}
    CountStat(llvm::StringRef key, llvm::raw_ostream& os, size_t count): Stat(os), key_(key), count(count) {}

    CountStat& operator++() {
      ++count;
      return *this;
    }

    CountStat& operator++(int) {
      ++count;
      return *this;
    }

    CountStat& operator+=(size_t i) {
      count += i;
      return *this;
    }

    llvm::StringRef key() const { return key_; }
    void print_value() {
      os << count;
    }
  };
  
}
