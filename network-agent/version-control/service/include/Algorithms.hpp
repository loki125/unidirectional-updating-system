#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>

#include "Graphs.hpp"

using json = nlohmann::json;

//reverse topological sort
class RTS {
private:
    // helper functions for internal use
    json extract_manifest_JSON(const std::string& tarPath);

    PackageGraph graph_builder(const json& manifest_json);

    std::vector<int> sort_algo(const Graph& graph);

    json phrase_sorted_vector(const PackageGraph& pgraph, const std::vector<int>& sorted_vector); 

public:
    static json sort(const std::string& file_name); 
};

// symetic linked forest 
class SLF{
    // under contsruction
}