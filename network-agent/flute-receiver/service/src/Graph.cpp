#include "Graph.hpp"
#include <stdexcept>

Graph::Graph(std::size_t node_count)
    : adj_(node_count)
{}

void Graph::add_edge(std::size_t from, std::size_t to) {
    if (from >= adj_.size() || to >= adj_.size()) {
        throw std::out_of_range("Graph::add_edge: invalid node index");
    }
    adj_[from].push_back(to);
}

const std::vector<std::size_t>& Graph::neighbors(std::size_t node) const {
    if (node >= adj_.size()) {
        throw std::out_of_range("Graph::neighbors: invalid node index");
    }
    return adj_[node];
}

std::size_t Graph::size() const noexcept {
    return adj_.size();
}
