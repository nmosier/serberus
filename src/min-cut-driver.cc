#include <iostream>

#include "min-cut.h"


int main() {
    using Alg = FordFulkersonMulti<int, int>;
    std::string tok;
    
    int num_entries;
    std::cerr << "num entries: ";
    std::cin >> num_entries;
    std::vector<Alg::Entry> entries (num_entries);
    for (Alg::Entry& entry : entries) {
        std::cerr << "source and sink: ";
        std::cin >> entry.st.s >> entry.st.t;
        int num_edges;
        std::cerr << "num edges: ";
        std::cin >> num_edges;
        for (int i = 0; i < num_edges; ++i) {
            int u, v, w;
            std::cerr << "edge: ";
            std::cin >> u >> v >> w;
            entry.G[u][v] = w;
        }
    }
    
    const int res = Alg().run(entries.begin(), entries.end());
    std::cout << res << "\n";
}
