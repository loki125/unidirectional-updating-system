#include "Recipe.hpp"

#include <spdlog/spdlog.h> 

RecipeMaker::RecipeMaker(const json& manifest) : 

packages(manifest[manifest::PACKAGES].get<std::vector<json>>()), 
global_sort(GSO(this->packages))
{}

void RecipeMaker::generate_recipe(const fs::path& directory_path, PackageReader& reader, const json& forest) {

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

    // Get Scripts
    recipe.update(json{recipe::SYMLINK_FOREST, forest});
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
