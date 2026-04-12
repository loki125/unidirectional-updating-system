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
    json recipe;
    
    bool is_system = reader.is_system_pkg(pkg_name);
    recipe[recipe::IS_SYSTEM] = is_system;
    if(is_system) {
        spdlog::info("Found recipe generation for system package: {}, beginning bundling", pkg_name);
        reader.bundle_system_package(store_vol, pkg_path.string(), this->global_sort.subgraph_order(pkg_name, pkg_version));
    }
    
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
    std::string sys_dep;
    
    for(const auto& pkg : full_deps){
        std::string current_dep = pkg[pkg::NAME].get<std::string>();
        std::string current_path = pkg[pkg::PATH].get<std::string>();

        if(reader.is_system_pkg(current_dep) && current_dep != pkg_name) {
            sys_dep = current_path; // Skip the system package itself (don't mount self to self)
            break;
        }

        required_mounts.push_back(current_path);       
    }
        
    json instr;
    instr[recipe::MOUNT_REQ] = required_mounts;
    instr[recipe::MOUNT_SYS] = sys_dep.empty() ? "" : sys_dep;
    return instr;
}