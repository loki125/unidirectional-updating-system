#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <unordered_set>
#include <set>
#include <stack>
#include <nlohmann/json.hpp>

#include "utils.hpp"
#include "Graphs.hpp"


//Global Sort Order
class GSO {
private:
    static PackageGraph graph_builder(const std::vector<json>& pkg_list);

    static std::vector<std::size_t> sort_algo(const Graph& graph);

    static std::unordered_set<std::size_t> get_descendants(const Graph& g, std::size_t start);

    static void resolve_circular_dependencies(Graph &graph);

    static std::vector<json> phrase_sorted_vector(const PackageGraph& pgraph, const std::vector<std::size_t>& sorted_vector); 

    PackageGraph pgraph;

    std::vector<json> sorted_pkgs;
    
public:
    GSO(const std::vector<json>& packages);

    const std::vector<json> subgraph_order(const std::string& name, const std::string& version) const;

    const std::vector<json> get_sorted_pkgs() const { 
        return sorted_pkgs; 
    }

    ~GSO() = default;

};


