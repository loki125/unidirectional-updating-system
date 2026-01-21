
#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>

#include "PackageGraph.hpp"

using json = nlohmann::json;

namespace Topologicalsorter {

json topo_sort(const std::string& file_name); 

json extract_manifest_JSON(const std::string& tarPath);

PackageGraph graph_builder(const json& manifest_json);

std::vector<int> topo_sort_algo(const Graph& graph);

json phrase_sorted_vector(const PackageGraph& pgraph, const std::vector<int>& sorted_vector);

}