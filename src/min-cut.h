#pragma once

#include <vector>
#include <map>
#include <set>
#include <queue>
#include <iostream>
#include <cassert>

std::vector<std::pair<int, int>> minCut(const std::vector<std::vector<int>>& graph, int s, int t);

/**
 * Ford-Fulkerson algorithm for multiple source/sink pairs.
 */
template <class Node, class Weight, class NodeLess = std::less<Node>>
struct FordFulkersonMultiOld {
    struct ST {
        Node s;
        Node t;
    };
    using Graph = std::map<Node, std::set<Node, NodeLess>, NodeLess>;
    using WGraph = std::map<Node, std::map<Node, Weight, NodeLess>, NodeLess>;
    struct Entry {
        ST st;
        Graph G;
    };
    
    struct Max {
        Weight operator()(Weight a, Weight b) const {
            return std::max(a, b);
        }
    };
    
    template <class InputIt>
    static Weight run(const WGraph& G, InputIt entry_begin, InputIt entry_end) {
        WGraph R = G; // residual
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
#if 0	    
            for (Node v = it->st.t; v != it->st.s; v = parent.at(v)) {
                const Node& u = parent.at(v);
                path_flow = std::min(path_flow, R[u][v]);
            }
#else
	    {
	      Node v = it->st.t;
	      do {
		const Node& u = parent.at(v);
		path_flow = std::min(path_flow, R[u][v]);
		v = u;
	      } while (v != it->st.s);
	    }
#endif
#if 0
            for (Node v = it->st.t; v != it->st.s; v = parent.at(v)) {
                const Node& u = parent.at(v);
                R[u][v] -= path_flow;
                R[v][u] += path_flow;
            }
#else
	    {
	      Node v = it->st.t;
	      do {
		const Node& u = parent.at(v);
		R[u][v] -= path_flow;
		R[v][u] += path_flow;
		v = u;
	      } while (v != it->st.s);
	    }
#endif
            
            max_flow += path_flow;
        }
        
        return max_flow;
    }
    
private:
    
    static bool bfs(WGraph& G, const Node& s, const Node& t, Graph& H, std::map<Node, Node, NodeLess>& parent) {
        std::set<Node, NodeLess> visited;

        std::queue<Node> q;
        q.push(s);
        visited.insert(s);

        while (!q.empty()) {
            Node u = q.front();
            q.pop();
            
            for (const Node& v : H[u]) {
                Weight w = G[u][v];
                if (w > 0) {
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


#if 0 
	std::queue<Node> q;
	q.push(s);

	while (!q.empty()) {
	  Node u = q.front();
	  q.pop();
	  if (visited.insert(u).second) {
	    for (const Node& v : H[u]) {
	      Weight w = G[u][v];
	      if (w > 0) {
		if (v == t) {
	      }
	    }
	  }
	}
#endif        
        return false;
    }
    
};


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
#if 0
            for (Node v = it->t; !it->s.contains(v); v = parent.at(v)) {
                const Node& u = parent.at(v);
                path_flow = std::min(path_flow, R[u][v]);
            }
            
            for (Node v = it->t; !it->s.contains(v); v = parent.at(v)) {
                const Node& u = parent.at(v);
                R[u][v] -= path_flow;
                R[v][u] += path_flow;
            }
#else
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
#endif
            
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
	    if (w > 0 && visited.contains(u) &&
#if 0
		!visited.contains(v)
#else
		R[u][v] <= 0
#endif
		) {
#if 0
	      assert(R[u][v] <= 0);
#endif
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
#if 0
        for (const Node& s_ : s) {
            q.push(s_);
            visited.insert(s_);
        }
        
        while (!q.empty()) {
            Node u = q.front();
            q.pop();
            
            for (const auto& vp : G[u]) {
                const Node& v = vp.first;
                Weight w = G[u][v];
                if (!visited.contains(v) && w > 0) {
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
#else
	for (const Node& s_ : s) {
	  q.push(s_);
	}

	while (!q.empty()) {
	  Node u = q.front();
	  q.pop();
	  if (visited.insert(u).second) {
	    for (const auto& [v, w] : G[u]) {
	      if (w > 0) {
#if 0
		parent[v] = u;
#else
		parent.emplace(v, u);
#endif
		if (v == t) {
		  return true;
		}
		q.push(v);
	      }
	    }
	  }
	}
#endif

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
