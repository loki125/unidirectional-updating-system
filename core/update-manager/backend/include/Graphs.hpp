#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <unordered_map>

#include "utils.hpp"

/**
 * @brief Simple directed graph using an adjacency list.
 */
class Graph {
public:
    /**
     * @brief Constructs a graph with a fixed number of nodes.
     * @param node_count Number of nodes in the graph.
     */
    explicit Graph(std::size_t node_count);

    /**
     * @brief Adds a directed edge between two nodes.
     * @param from Source node index.
     * @param to Destination node index.
     */
    void add_edge(std::size_t from, std::size_t to);

    /**
     * @brief Removes a directed edge between two nodes.
     * @param from Source node index.
     * @param to Destination node index.
     */
    void rm_edge(std::size_t from, std::size_t to);

    /**
     * @brief Retrieves the neighbors of a specific node.
     * @param node Node index.
     * @return const std::vector<std::size_t>& List of adjacent node indices.
     */
    const std::vector<std::size_t>& neighbors(std::size_t node) const;

    /**
     * @brief Gets the number of nodes in the graph.
     * @return std::size_t Node count.
     */
    std::size_t size() const noexcept;

private:
    std::vector<std::vector<std::size_t>> adj_; ///< Internal adjacency list.
};

/**
 * @brief Represents a package and its dependency tree.
 */
struct Package {
    std::string name; ///< Package name.
    std::string version; ///< Package version.
    PackageMetadata package_metadata; ///< Detailed metadata for the package.
    std::vector<Package> dependencies; ///< List of direct dependencies.

    /**
     * @brief Constructs a package from metadata.
     * @param pkg_metadata Metadata containing name and version.
     */
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

/**
 * @brief Hash provider for Package objects using name and version.
 */
struct PackageHash {
    /**
     * @brief Generates a hash for a Package.
     * @param k The package to hash.
     * @return std::size_t Calculated hash value.
     */
    std::size_t operator()(const Package& k) const {
        return std::hash<std::string>{}(k.name) ^ (std::hash<std::string>{}(k.version) << 1);
    }
};

/**
 * @brief Manages a graph where nodes represent packages and edges represent dependencies.
 */
class PackageGraph {
public:
    /**
     * @brief Constructs a graph from a list of packages and maps their dependencies.
     * @param packages Vector of packages to include.
     */
    explicit PackageGraph(const std::vector<Package>& packages);

    /**
     * @brief Constructs an empty package graph with a specified size.
     * @param size Initial node capacity.
     */
    explicit PackageGraph(std::size_t size) : graph_(size) {}

    /** @brief Returns a const reference to the underlying graph. */
    const Graph& graph() const;

    /** @brief Returns a reference to the underlying graph. */
    Graph& graph();

    /**
     * @brief Gets a package by its internal ID.
     * @param id The node index.
     * @return Package& Reference to the package object.
     */
    const Package& get_package(std::size_t id) const;
    Package& get_package(std::size_t id);

    /**
     * @brief Finds the internal ID for a given package.
     * @param pkg The package to look up.
     * @return std::optional<std::size_t> The ID if found, otherwise std::nullopt.
     */
    std::optional<std::size_t> get_id(const Package& pkg) const;

    /**
     * @brief Adds a package and its dependencies as edges in the graph.
     * @param pkg The root package to add.
     */
    void add_depend(const Package& pkg);

private:
    Graph graph_; ///< The underlying directed graph.
    
    std::vector<Package> id_to_pkg; ///< Mapping from node ID to Package object.
    std::unordered_map<Package, std::size_t, PackageHash> pkg_to_id; ///< Mapping from Package to node ID.

    /**
     * @brief Adds a package to internal maps and returns its ID, updating metadata if necessary.
     * @param pkg Package to add.
     * @return std::size_t Internal ID assigned to the package.
     */
    std::size_t add_pkg(const Package& pkg);
};