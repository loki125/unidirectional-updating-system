#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Graph.hpp"

// The Unified Struct
struct Package {
    std::string name;
    std::string version;
    std::vector<Package> dependencies; // Recursive definition

    bool operator==(const Package& other) const {
        return name == other.name && version == other.version;
    }
};

// The Hash function 
struct PackageHash {
    std::size_t operator()(const Package& k) const {
        // Standard hash combining
        return std::hash<std::string>{}(k.name) ^ (std::hash<std::string>{}(k.version) << 1);
    }
};

class PackageGraph {
public:
    explicit PackageGraph(const std::vector<Package>& packages);
    explicit PackageGraph(int size) : graph_(size) {}

    const Graph& graph() const;

    const Package& info_of(int id) const; 

    void add_depend(const Package& pkg);

private:
    Graph graph_;
    
    std::vector<Package> id_to_pkg;
    std::unordered_map<Package, int, PackageHash> pkg_to_id;

    int add_pkg(const Package& pkg);
};