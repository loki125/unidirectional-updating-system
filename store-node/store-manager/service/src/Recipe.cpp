#include "Recipe.hpp"

#include <spdlog/spdlog.h> 

RecipeMaker::RecipeMaker(const json& manifest) : 

packages(manifest[manifest::PACKAGES].get<std::vector<json>>()), 
global_sort(GSO(this->packages))
{}


void RecipeMaker::generate_recipe(const fs::path& directory_path, PackageReader& reader, const provider_vector& provider_vector, const json& forest) {

    fs::path pkg_path = reader.get_pkg_path(directory_path);
    fs::path store_vol = directory_path.parent_path();

    std::string pkg_name = reader.get_name(pkg_path.string());
    std::string pkg_version = reader.get_version(pkg_path.string());
    
    if(reader.is_system_pkg(pkg_name)) {
        spdlog::info("Found recipe generation for system package: {}, beginning bundling", pkg_name);
        reader.bundle_system_package(store_vol, pkg_path.string(), this->global_sort.subgraph_order(pkg_name, pkg_version));
    }
    
    json recipe;
    recipe[recipe::PACKAGE_NAME] = pkg_name;
    recipe[recipe::VERSION] = pkg_version;

    // Calculate Recursive Mounts (Pass the reader to allow system dependency filtering)
    json mount_instr = calculate_mounts(pkg_name, pkg_version, reader);
    recipe[recipe::MOUNT_INS] = mount_instr[recipe::MOUNT_REQ].empty() ? json::array() : mount_instr;

    recipe[recipe::SYMLINK_FOREST] = forest;

    std::vector<std::string> temp_list;
    temp_list.reserve(provider_vector.size()); 
    for (const auto& [name, soname] : provider_vector) {
        temp_list.push_back(name);
    }
    recipe[recipe::PROVIDER_MAP] = temp_list; 
        
    recipe[recipe::SCRIPTS] = reader.get_scripts(pkg_path.string());

    fs::path recipe_out = directory_path / recipe::FILENAME;
    std::ofstream out(recipe_out);
    out << recipe.dump(4);
    out.close();

    spdlog::info("Recipe generated successfully at {}", recipe_out.string());
}

json RecipeMaker::calculate_mounts(const std::string& pkg_name, const std::string& pkg_version, PackageReader& reader) {
    json required_mounts = json::array();
    
    auto full_deps = this->global_sort.subgraph_order(pkg_name, pkg_version);
    std::unordered_set<std::string> bundled_deps;
    
    // 1. Identify dependencies that are bundled inside a system package
    for (const auto& dep : full_deps) {
        std::string dep_name = dep[pkg::NAME].get<std::string>();
        
        // If we encounter a system package (that isn't the root package itself)
        if (reader.is_system_pkg(dep_name) && dep_name != pkg_name) {
            auto sys_deps = this->global_sort.subgraph_order(dep_name, dep[pkg::VERSION].get<std::string>());
            for (const auto& sys_dep : sys_deps) {
                std::string sys_dep_name = sys_dep[pkg::NAME].get<std::string>();
                if (sys_dep_name != dep_name) { // Add its dependencies, but not the system package itself
                    bundled_deps.insert(sys_dep_name);
                }
            }
        }
    }
    
    // 2. Build mounts, strictly omitting anything that is bundled
    for(const auto& pkg : full_deps){
        std::string current_dep = pkg[pkg::NAME].get<std::string>();
        if (bundled_deps.find(current_dep) == bundled_deps.end()) {
            required_mounts.push_back(pkg[pkg::PATH]);
        }
    }
        
    json instr;
    instr[recipe::MOUNT_REQ] = required_mounts;
    return instr;
}