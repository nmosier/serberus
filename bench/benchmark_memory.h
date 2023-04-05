#pragma once

#include <cstddef>
#include <cassert>

namespace benchmark {

  class State {
  public:
    State(size_t range_): range_(range_) {}
    
    const int *begin() const { return &dummy; }
    const int *end() const { return &dummy + 1; }

    size_t range(unsigned dim) const {
      assert(dim == 0);
      return range_;
    }

    

  private:
    size_t range_;
    int dummy;
  };
  
}
