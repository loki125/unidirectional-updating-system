#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdlib>
#include <nlohmann/json.hpp>

#include "utils.hpp"
#include "Algorithms.hpp"
#include "Graphs.hpp"
#include "PackageReader.hpp"


class RecipeMaker {
private:
    std::vector<json>  packages;
    GSO global_sort;

    // Helper to calculate mounts 
    json calculate_mounts(const std::string& pkg_name, const std::string& pkg_version);

public:
    RecipeMaker(const json& manifest);

    void generate_recipe(const fs::path& directory_path, PackageReader& reader, const provider_vector& provider_vector, const json& forest);

    const GSO& get_global_sort() const { return global_sort; }

};