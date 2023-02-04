#pragma once

#include <vector>
#include <utility>
#include <map>
#include <functional>
#include <cstddef>

namespace clou {

  std::vector<std::pair<int, int>> ford_fulkerson(unsigned n, const std::vector<std::map<unsigned, unsigned>>& G,
						  unsigned s, unsigned t);
  
}
