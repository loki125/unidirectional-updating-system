#pragma once
#include <string>
#include <unordered_map>

#include "Graph.hpp"

struct Package {
    std::string name;
    std::string version;
    std::vector<std::pair<std::string, std::string>> dependencies;
};

class PackageGraph {
public:
    explicit PackageGraph(const std::vector<Package>& packages);
    explicit PackageGraph(int size): graph_(size){}

    const Graph& graph() const;
    const std::pair<std::string, std::string>& name_of(int id) const;
    void add_depend(const Package& pkg);
    

private:
    Graph graph_;
    std::vector<std::pair<std::string, std::string>> id_to_pkg;
    std::unordered_map<std::pair<std::string, std::string>, int> pkg_to_id;

    int add_pkg(const std::string& pkg_name, const std::string& pkg_version);
};