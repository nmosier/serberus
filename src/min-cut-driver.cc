#include <iostream>

#include "MinCutSMT.h"

int main() {
  using Alg = clou::MinCutSMT<int, int>;
  Alg A;
  std::string tok;
    
  Alg::Graph& G = A.G;
  std::cerr << "weighted graph, num edges: ";
  int num_edges;
  std::cin >> num_edges;
  for (int i = 0; i < num_edges; ++i) {
    int u, v, w;
    std::cerr << "weighted edge: ";
    std::cin >> u >> v >> w;
    G[u][v] = w;
  }
    
  int num_sts;
  std::cerr << "num st pairs: ";
  std::cin >> num_sts;
  for (int i = 0; i < num_sts; ++i) {
    std::cerr << "st pair: ";
    int s, t;
    std::cin >> s >> t;
    A.add_st(s, t);
  }
    
  const auto& cut_edges = A.cut_edges;
  A.run();
  for (const auto& cut_edge : cut_edges) {
    std::cout << cut_edge.src << " " << cut_edge.dst << "\n";
  }
}
