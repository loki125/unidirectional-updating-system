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


//Global Sort Order
class GSO {
private:
    static PackageGraph graph_builder(const std::vector<PackageMetadata>& pkg_list);

    static std::vector<std::size_t> sort_algo(const Graph& graph);

    static std::unordered_set<std::size_t> get_descendants(const Graph& g, std::size_t start);

    static std::vector<EdgeToCut> scc_detection(const Graph &graph);

    void resolve_scc(const std::vector<EdgeToCut>& edges_to_cut);

    static std::vector<PackageMetadata> phrase_sorted_vector(const PackageGraph& pgraph, const std::vector<std::size_t>& sorted_vector); 

    PackageGraph pgraph;

    std::vector<PackageMetadata> sorted_pkgs;
    
public:
    GSO(const std::vector<PackageMetadata>& packages);

    const std::vector<PackageMetadata> subgraph_order(const std::string& name, const std::string& version) const;

    const std::vector<PackageMetadata> get_sorted_pkgs() const { 
        return sorted_pkgs; 
    }

    ~GSO() = default;

};


