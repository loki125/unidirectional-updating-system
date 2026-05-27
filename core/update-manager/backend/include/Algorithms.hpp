#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <unordered_set>
#include <set>
#include <stack>

#include "utils.hpp"
#include "Graphs.hpp"

/**
 * @brief Global Sort Order (GSO) manages the dependency graph and provides topological sorting.
 */
class GSO {
private:

    struct SCCState {
        std::vector<int> disc;
        std::vector<int> low;
        std::vector<bool> stackMember;
        std::stack<std::size_t> st;
        std::size_t timer = 0;
        std::set<EdgeToCut> edges_to_cut;

        SCCState(std::size_t n) 
            : disc(n, -1), low(n, -1), stackMember(n, false) {}
    };

    /**
     * @brief Performs a depth-first search for identifying strongly connected components using Tarjan's algorithm.
     * @param u The index of the current node being visited.
     * @param graph The directed graph to traverse.
     * @param state The state object tracking discovery times and low-link values.
     */
    static void scc_dfs(std::size_t u, const Graph &graph, SCCState &state);

    /**
     * @brief Internal helper to construct a PackageGraph from a list of metadata.
     * @param pkg_list A vector of PackageMetadata to be represented in the graph.
     * @return A constructed PackageGraph instance.
     */
    static PackageGraph graph_builder(const std::vector<PackageMetadata>& pkg_list);

    /**
     * @brief Performs a topological sort using Kahn's algorithm.
     * @param graph The underlying adjacency list representation.
     * @return A vector of node indices in topological order (dependency-first).
     */
    static std::vector<std::size_t> sort_algo(const Graph& graph);

    /**
     * @brief Performs a depth-first search to find all descendants of a node.
     * @param g The graph to traverse.
     * @param start The starting node index.
     * @return A set of indices representing all reachable descendants.
     */
    static std::unordered_set<std::size_t> get_descendants(const Graph& g, std::size_t start);

    /**
     * @brief Detects strongly connected components (cycles) within the graph.
     * @param graph The graph to analyze.
     * @return A list of edges that contribute to cycles and need resolution.
     */
    static std::vector<EdgeToCut> scc_detection(const Graph &graph, const std::vector<std::size_t>& root_nodes);

    /** 
    * @brief detect the current root of the graph
    * @param graph The graph to analyze.
    * @return A list of the root nodes.
    */
    static std::vector<std::size_t> resolve_roots(const Graph &graph);

    /**
     * @brief Resolves detected cycles based on their conflict type (SOFT or HARD).
     * @param edges_to_cut The list of cycle-causing edges found by scc_detection.
     * @throws HardConflictException if a complex cycle cannot be resolved automatically.
     */
    void resolve_scc(std::vector<EdgeToCut>& edges_to_cut);

    /**
     * @brief Maps a sorted list of indices back to a list of PackageMetadata.
     * @param pgraph The package graph containing metadata associations.
     * @param sorted_vector The topologically sorted indices.
     * @return A vector of sorted PackageMetadata.
     */
    static std::vector<PackageMetadata> phrase_sorted_vector(const PackageGraph& pgraph, const std::vector<std::size_t>& sorted_vector); 

    PackageGraph pgraph; ///< The internal package-aware graph structure.

    std::vector<PackageMetadata> sorted_pkgs; ///< The final sorted list of packages.
    
public:
    /**
     * @brief Constructs a GSO object and triggers the resolution and sorting process.
     * @param packages The initial list of packages to sort and validate.
     */
    GSO(const std::vector<PackageMetadata>& packages);

    /**
     * @brief Retrieves the dependency subgraph for a specific package, sorted correctly.
     * @param name The name of the package.
     * @param version The version of the package.
     * @return A vector of PackageMetadata representing the package and all its dependencies.
     */
    const std::vector<PackageMetadata> subgraph_order(const std::string& name, const std::string& version) const;

    /**
     * @brief Returns the full globally sorted list of packages.
     * @return A constant reference to the sorted package vector.
     */
    const std::vector<PackageMetadata> get_sorted_pkgs() const { 
        return sorted_pkgs; 
    }

    ~GSO() = default;

};
