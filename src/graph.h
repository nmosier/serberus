#pragma once

#include <map>
#include <vector>

template <class Node, class NodeLess = std::less<Node>> class Graph {
public:
  using Value = int;
  using NodeVec = std::vector<Node>;
  using Vertex = typename NodeVec::size_type;
  using Edges = std::vector<std::vector<Value>>;

  Vertex lookup_or_add_node(const Node &node) {
    const auto res = vertex_to_node.emplace(node, nodes.size());
    if (res.second) {
      nodes.push_back(node);
      for (auto &edge : edges) {
        edge.resize(num_vertices(), 0);
      }
      edges.resize(num_vertices(), std::vector<Value>(num_vertices(), 0));
    }
    return res.first->second;
  }

  void add_node(const Node &node) { lookup_or_add_node(node); }

  Vertex lookup_node(const Node &node) const { return vertex_to_node.at(node); }

  void add_edge(const Node &src, const Node &dst, Value weight) {
    const Vertex vsrc = lookup_or_add_node(src);
    const Vertex vdst = lookup_or_add_node(dst);
    edges[vsrc][vdst] = weight;
  }

  void remove_edge(const Node &src, const Node &dst) { add_edge(src, dst, 0); }

  std::vector<std::vector<Value>> adjacency_array() const { return edges; }

  Vertex num_vertices() const { return nodes.size(); }

  bool empty() const { return nodes.empty(); }

  const Node &lookup_vert(Vertex v) const { return nodes.at(v); }

  /** Get edge weight by vertex. */
  Value get_edge_v(Vertex src, Vertex dst) const { return edges[src][dst]; }

private:
  NodeVec nodes;
  using NodeMap = std::map<Node, Vertex, NodeLess>;
  NodeMap vertex_to_node;
  Edges edges;
};
