#pragma once

#include "MinCutBase.h"

namespace clou {

template <class Node, class Weight, class NodeLess = std::less<Node>>
struct FordFulkersonMulti {
    struct ST {
        std::set<Node, NodeLess> s;
        Node t;
    };
    using Graph = std::map<Node, std::map<Node, Weight, NodeLess>, NodeLess>;
    
    template <class InputIt, class OutputIt>
    static OutputIt run(const Graph& G, InputIt begin, InputIt end, OutputIt out) {
        Graph R = G;
        Weight max_flow = 0;
        while (true) {
            std::map<Node, Node, NodeLess> parent;
            InputIt it;
            for (it = begin; it != end; ++it) {
                parent.clear();
                if (bfs(R, it->s, it->t, parent)) {
                    break;
                }
            }
            if (it == end) {
                // found no paths
                break;
            }
            
            Weight path_flow = std::numeric_limits<Weight>::max();
	    {
	      Node v;
	      v = it->t;
	      do {
		const Node& u = parent.at(v);
                path_flow = std::min(path_flow, R[u][v]);
		v = u;
	      } while (!it->s.contains(v));
	      v = it->t;
	      do {
                const Node& u = parent.at(v);
                R[u][v] -= path_flow;
		v = u;
	      } while (!it->s.contains(v));
	    }
            
            max_flow += path_flow;
            
        }
        
        std::set<Node, NodeLess> visited;
        for (auto it = begin; it != end; ++it) {
            for (const Node& s_ : it->s) {
                dfs(R, s_, visited);
            }
        }
        
        // get all edges from reachable vertex to unreachable
        for (const auto& [u, vs] : G) {
	  for (const auto& [v, w] : vs) {
	    if (w > 0 && visited.contains(u) && R[u][v] <= 0) {
	      *out++ = std::make_pair(u, v);
            }
	  }
	}
        
        return out;
    }
    
private:
    static bool bfs(Graph& G, const std::set<Node, NodeLess>& s, const Node& t, std::map<Node, Node, NodeLess>& parent) {
        std::set<Node, NodeLess> visited;

        std::queue<Node> q;
	for (const Node& s_ : s) {
	  q.push(s_);
	}

	while (!q.empty()) {
	  Node u = q.front();
	  q.pop();
	  if (visited.insert(u).second) {
	    for (const auto& [v, w] : G[u]) {
	      if (w > 0) {
		parent.emplace(v, u);
		if (v == t) {
		  return true;
		}
		q.push(v);
	      }
	    }
	  }
	}

        return false;
    }
    
    static void dfs(Graph& G, const Node& s, std::set<Node, NodeLess>& visited) {
        if (visited.insert(s).second) {
            for (const auto& up : G[s]) {
                const Node& u = up.first;
                const Weight w = up.second;
                if (w) {
                    dfs(G, u, visited);
                }
            }
        }
    }
};

}
