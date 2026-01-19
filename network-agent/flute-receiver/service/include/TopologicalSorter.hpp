
#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>

#include "PackageGraph.hpp"

using json = nlohmann::json;

namespace Topologicalsorter {

json topo_sort(const std::string& file_name); 

json extract_manifest_JSON(const std::string& tarPath);

Graph graph_builder(const json& manifest_json);

}