#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include "utils.hpp"

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

    Package(const json pkg_json) {
        package_json = pkg_json;

        name = package_json.at(pkg::NAME).get<std::string>();
        version = package_json.at(pkg::VERSION).get<std::string>();
    }
    Package(const std::string& name, const std::string& version) : name(name), version(version) {}
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
    explicit PackageGraph(std::size_t size) : graph_(size) {}

    const Graph& graph() const;

    Graph& graph();

    const Package& get_package(std::size_t id) const;

    std::optional<std::size_t> get_id(const Package& pkg) const;

    void add_depend(const Package& pkg);

private:
    Graph graph_;
    
    std::vector<Package> id_to_pkg;
    std::unordered_map<Package, std::size_t, PackageHash> pkg_to_id;

    std::size_t add_pkg(const Package& pkg);
};