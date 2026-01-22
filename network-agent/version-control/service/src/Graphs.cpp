#include "Graphs.hpp"
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

PackageGraph::PackageGraph(const std::vector<Package>& packages)
    : graph_(packages.size())
{
    for(const Package& pkg : packages)
        this->add_depend(pkg);
    
}

const Graph& PackageGraph::graph() const
{
    return this->graph_;
}

const Package &PackageGraph::get_package(int id) const
{
    return this->id_to_pkg[id];
}

void PackageGraph::add_depend(const Package &pkg)
{
    int pkg_id = this->add_pkg(pkg);
    
    for( const auto& depend : pkg.dependencies){
        int depend_id = this->add_pkg(depend);

        // Add the edge
        graph_.add_edge(pkg_id, depend_id);
    }
}

int PackageGraph::add_pkg(const Package& pkg){
    
    if (pkg_to_id.find(pkg) == pkg_to_id.end()) {
        size_t new_id = id_to_pkg.size();
        id_to_pkg.push_back(pkg);
        pkg_to_id[pkg] = new_id;
    }
    return pkg_to_id[pkg];
}

