#pragma once

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdlib>
#include <unordered_map>
#include <set>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

#include "Graphs.hpp"
#include "PackageReader.hpp"

//Global Sort Order
class GSO {
private:
    static PackageGraph graph_builder(const json& manifest_json);

    static std::vector<int> sort_algo(const Graph& graph);

    static void resolve_circular_dependencies(Graph &graph);

    static std::vector<json> phrase_sorted_vector(const PackageGraph& pgraph, const std::vector<int>& sorted_vector); 
    
public:
    static std::vector<json> sort(const json &manifest_json); 

    static std::unordered_map<std::string, int> get_global_priority_map(const std::vector<json>& sorted_pkgs);
};



class RecipeMaker {
private:
    json global_manifest;
    std::unordered_map<std::string, int> priority_map;
    std::unordered_map<std::string, json> pkg_lookup;

public:
    RecipeMaker(const json& manifest);

    void generate_recipe(const fs::path& directory_path, const std::string& type);

private:
    // Helper to calculate mounts 
    json calculate_mounts(const std::string& my_name, const std::vector<std::string>& direct_deps);
};