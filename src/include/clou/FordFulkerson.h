#pragma once

#include <vector>
#include <utility>
#include <map>
#include <functional>
#include <cstddef>
#include <cassert>
#include <vector>
#include <set>

#include <llvm/ADT/ArrayRef.h>

namespace clou {

  std::vector<std::pair<int, int>> ford_fulkerson(unsigned n,
						  std::vector<std::map<unsigned, unsigned>>& G,
						  unsigned s, unsigned t);

  std::vector<std::pair<unsigned, unsigned>>
  ford_fulkerson_multi(const std::vector<std::map<unsigned, unsigned>>& G, llvm::ArrayRef<std::set<unsigned>> waypoint_sets);
  
}
