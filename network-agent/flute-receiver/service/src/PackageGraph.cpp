#include "PackageGraph.hpp"

PackageGraph::PackageGraph(const std::vector<Package>& packages)
    : graph_(packages.size())
{
    for(const Package& pkg : packages){
        this->add_depend(pkg);
    }
}

const Graph &PackageGraph::graph() const
{
    return this->graph_;
}

const std::pair<std::string, std::string> &PackageGraph::name_of(int id) const
{
    return id_to_pkg[id];
}

void PackageGraph::add_depend(const Package &pkg)
{
    int pkg_id = this->add_pkg(pkg.name, pkg.version);
    
    for( const auto& depend : pkg.dependencies){
        int depend_id = this->add_pkg(depend.first, depend.second);

        // Add the edge
        graph_.add_edge(pkg_id, depend_id);
    }
}

int PackageGraph::add_pkg(const std::string& pkg_name, const std::string& pkg_version){
    std::pair pkg = {pkg_name, pkg_version};
    
    if (pkg_to_id.find(pkg) == pkg_to_id.end()) {
        size_t new_id = id_to_pkg.size();
        id_to_pkg.push_back(pkg);
        pkg_to_id[pkg] = new_id;
    }
    return pkg_to_id[pkg];
}
