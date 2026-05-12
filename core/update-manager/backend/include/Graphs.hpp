#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <unordered_map>

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
    PackageMetadata package_metadata;
    std::vector<Package> dependencies; // Recursive definition

    Package(const PackageMetadata pkg_metadata) {
        package_metadata = pkg_metadata;

        name = package_metadata.Package;
        version = package_metadata.Version;
    }
    Package(const std::string& name, const std::string& version) : name(name), version(version) {}
    Package() = default;

    bool operator==(const Package& other) const {
        return name == other.name && version == other.version;
    }
};

// The Hash function Generates a size_t (64bits) FROM xOR name & version to be used as an id for unordered_map
struct PackageHash {
    std::size_t operator()(const Package& k) const {
        // std::hash -> MurmurHash/ CityHash
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

    Package& get_package(std::size_t id);

    std::optional<std::size_t> get_id(const Package& pkg) const;

    void add_depend(const Package& pkg);

private:
    Graph graph_;
    
    std::vector<Package> id_to_pkg;
    std::unordered_map<Package, std::size_t, PackageHash> pkg_to_id;

    std::size_t add_pkg(const Package& pkg);
};