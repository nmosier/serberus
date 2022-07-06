#pragma once

#include <vector>
#include <map>
#include <set>
#include <queue>

std::vector<std::pair<int, int>> minCut(const std::vector<std::vector<int>>& graph, int s, int t);

/**
 * Ford-Fulkerson algorithm for multiple source/sink pairs.
 */
template <class Node, class Weight, class NodeLess = std::less<Node>>
struct FordFulkersonMulti {
    struct ST {
        Node s;
        Node t;
    };
    using Graph = std::map<Node, std::map<Node, Weight, NodeLess>, NodeLess>;
    struct Entry {
        ST st;
        Graph G;
    };
    
    struct Max {
        Weight operator()(Weight a, Weight b) const {
            return std::max(a, b);
        }
    };
    
    template <class InputIt, class Combine = Max>
    static Weight run(InputIt entry_begin, InputIt entry_end, Combine combine = Combine()) {
        // construct composite graph
        Graph G;
        for (InputIt it = entry_begin; it != entry_end; ++it) {
            for (const auto& p_src : it->G) {
                const Node& src = p_src.first;
                auto& dsts = G[src];
                for (const auto& p_dst : p_src.second) {
                    const Node& dst = p_dst.first;
                    dsts[dst] = combine(dsts[dst], p_dst.second);
                }
            }
        }
        
        Graph R = G; // residual
        Weight max_flow = 0;
        while (true) {
            std::map<Node, Node, NodeLess> parent;
            InputIt it;
            for (it = entry_begin; it != entry_end; ++it) {
                parent.clear();
                if (bfs(R, it->st.s, it->st.t, it->G, parent)) {
                    break;
                }
            }
            if (it == entry_end) {
                // found no paths
                break;
            }
            
            Weight path_flow = std::numeric_limits<Weight>::max();
            for (Node v = it->st.t; v != it->st.s; v = parent.at(v)) {
                const Node& u = parent.at(v);
                path_flow = std::min(path_flow, R[u][v]);
            }
            
            for (Node v = it->st.t; v != it->st.s; v = parent.at(v)) {
                const Node& u = parent.at(v);
                R[u][v] -= path_flow;
                R[v][u] += path_flow;
            }
            
            max_flow += path_flow;
        }
        
        return max_flow;
    }
    
private:
    
    static bool bfs(Graph& G, const Node& s, const Node& t, Graph& H, std::map<Node, Node, NodeLess>& parent) {
        std::set<Node, NodeLess> visited;
        
        std::queue<Node> q;
        q.push(s);
        visited.insert(s);
        
        while (!q.empty()) {
            Node u = q.front();
            q.pop();
            
            for (const auto& vp : H[u]) {
                const Node& v = vp.first;
                Weight w = G[u][v];
                if (vp.second > 0 && w > 0) {
                    if (v == t) {
                        parent[v] = u;
                        return true;
                    }
                    q.push(v);
                    parent[v] = u;
                    visited.insert(v);
                }
            }
        }
        
        return false;
    }
    
};
