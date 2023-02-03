#pragma once

#include <vector>
#include <utility>
#include <map>

namespace clou {

  std::vector<std::pair<int, int>> ford_fulkerson(int n, const std::vector<std::map<int, int>>& G, int s, int t);
  
}
