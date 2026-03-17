#include "Recipe.hpp"

#include <spdlog/spdlog.h> 

RecipeMaker::RecipeMaker(const json& manifest) : 

packages(manifest[manifest::PACKAGES].get<std::vector<json>>()), 
global_sort(GSO(this->packages))
{}

void RecipeMaker::generate_recipe(const fs::path& directory_path, PackageReader& reader, const provider_vector& provider_vector,const json& forest) {

    fs::path pkg_path = reader.get_pkg_path(directory_path);

    // Extract Info using the Reader
    std::string pkg_name = reader.get_name(pkg_path.string());
    std::string pkg_version = reader.get_version(pkg_path.string());
    json recipe;
    
    recipe[recipe::PACKAGE_NAME] = pkg_name;
    recipe[recipe::VERSION] = pkg_version;

    // Calculate Recursive Mounts
    json mount_instr = calculate_mounts(pkg_name, pkg_version);
    recipe[recipe::MOUNT_INS] = mount_instr[recipe::MOUNT_REQ].empty() ? json::array() : mount_instr;

    // Add the forest and provider map for this package
    recipe[recipe::SYMLINK_FOREST] = forest;

    std::vector<std::string> temp_list;
    temp_list.reserve(provider_vector.size()); // Pre-allocate memory
    
    for (const auto& [name, soname] : provider_vector) {
        temp_list.push_back(name);
    }
    recipe[recipe::PROVIDER_MAP] = temp_list; // Single assignment
        
    // Get Scripts
    recipe[recipe::SCRIPTS] = reader.get_scripts(pkg_path.string());

    // Write Output
    fs::path recipe_out = directory_path / recipe::FILENAME;
    std::ofstream out(recipe_out);
    out << recipe.dump(4);
    out.close();

    spdlog::info("Recipe generated successfully at {}", recipe_out.string());
}


json RecipeMaker::calculate_mounts(const std::string& pkg_name, const std::string& pkg_version) {
    
    json required_mounts = json::array();
    for(const auto& pkg : this->global_sort.subgraph_order(pkg_name, pkg_version))
        required_mounts.push_back(pkg[pkg::PATH]);
        
    json instr;
    instr[recipe::MOUNT_REQ] = required_mounts;
    return instr;
}
