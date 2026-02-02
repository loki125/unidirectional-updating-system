#pragma once

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <set>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

#include "Graphs.hpp"

using json = nlohmann::json;

//reverse topological sort
class RTS {
private:
    static PackageGraph graph_builder(const json& manifest_json);

    static std::vector<int> sort_algo(const Graph& graph);

    static void resolve_circular_dependencies(Graph &graph);

    static std::vector<json> phrase_sorted_vector(const PackageGraph& pgraph, const std::vector<int>& sorted_vector); 
    
public:
    static std::vector<json> sort(const json &manifest_json); 
};

// symetic linked forest 
class SLF {
private:
    // Helper: Check if a file is executable
    static bool is_executable(const fs::path& p);

    /**
     * Unpacks .deb, scans for .so/bins, generates JSON, cleans up.
     */
    static void deb_inspector(const fs::path& store_path, const std::string& deb_filename);

public:
    /**
     * Entry point for building the SLF instructions.
     */
    static void build_slf(const json &package, const fs::path& store_volume);


};