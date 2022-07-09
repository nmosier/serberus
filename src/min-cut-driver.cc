#include <iostream>

#include "min-cut.h"


int main() {
    using Alg = FordFulkersonMulti<int, int>;
    std::string tok;
    
    Alg::Graph G;
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
    std::vector<Alg::ST> sts (num_sts);
    for (auto& st : sts) {
        std::cerr << "source(s): ";
        int num_sources;
        std::cin >> num_sources;
        for (int i = 0; i < num_sources; ++i) {
            int source;
            std::cin >> source;
            st.s.insert(source);
        }
        std::cerr << "sink: ";
        std::cin >> st.t;
    }
    
    std::vector<std::pair<int, int>> cut_edges;
    Alg::run(G, sts.begin(), sts.end(), std::back_inserter(cut_edges));
    for (const auto& cut_edge : cut_edges) {
        std::cout << cut_edge.first << " " << cut_edge.second << "\n";
    }
}
