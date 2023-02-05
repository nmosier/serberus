#pragma once

#include <vector>
#include <utility>
#include <map>
#include <functional>
#include <cstddef>
#include <cassert>
#include <vector>

#include <llvm/ADT/ScopedHashTable.h>

namespace clou {

  std::vector<std::pair<int, int>> ford_fulkerson(unsigned n,
						  std::vector<std::map<unsigned, unsigned>>& G,
						  unsigned s, unsigned t);

  
}
