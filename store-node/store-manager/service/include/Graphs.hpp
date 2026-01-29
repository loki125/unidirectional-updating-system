#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class Graph {
public:
    explicit Graph(std::size_t node_count);

    void add_edge(std::size_t from, std::size_t to);

    void rm_edge(std::size_t from, std::size_t to);

    const std::vector<std::size_t>& neighbors(std::size_t node) const;

    std::size_t size() const noexcept;

private:
    std::vector<std::vector<std::size_t>> adj_;
};

// The Unified Struct
struct Package {
    std::string name;
    std::string version;
    json package_json;
    std::vector<Package> dependencies; // Recursive definition

    Package(const json& pkg_json) {
        package_json = pkg_json;

        name = package_json.at("Package").get<std::string>();
        version = package_json.at("Version").get<std::string>();
    }
    Package() = default;

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

    Graph& graph();

    const Package& get_package(int id) const; 

    void add_depend(const Package& pkg);

private:
    Graph graph_;
    
    std::vector<Package> id_to_pkg;
    std::unordered_map<Package, int, PackageHash> pkg_to_id;

    int add_pkg(const Package& pkg);
};