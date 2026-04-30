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
    recipe[recipe::MOUNT_INS] = mount_instr.empty() ? json::array() : mount_instr;

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

    spdlog::debug("Recipe generated successfully at {}", recipe_out.string());
}

json RecipeMaker::calculate_mounts(const std::string& pkg_name, const std::string& pkg_version, PackageReader& reader) {
    std::vector<std::string> required_mounts_vec;
    std::string sys_dep_path;

    // 1. Get the full subgraph (Deepest base dependencies first, target package last)
    auto full_deps = this->global_sort.subgraph_order(pkg_name, pkg_version);
    
    std::unordered_set<std::string> sys_subgraph_names;

    // 2. Search BACKWARDS from the target package down to its dependencies
    // This ensures we hit 'libc6' BEFORE we hit its deep dependency 'libc-gconv'
    for (auto it = full_deps.rbegin(); it != full_deps.rend(); ++it) {
        const auto& pkg = *it;
        std::string current_dep = pkg[pkg::NAME].get<std::string>();
        
        // Skip the target package itself during this search
        if (current_dep == pkg_name) {
            continue; 
        }

        if (reader.is_system_pkg(current_dep)) {
            // We found the HIGHEST level system package (e.g., libc6)!
            sys_dep_path = pkg[pkg::PATH].get<std::string>();
            
            // Get the system package's subgraph
            std::string sys_ver = pkg[pkg::VERSION].get<std::string>(); 
            auto sys_subgraph = this->global_sort.subgraph_order(current_dep, sys_ver);
            
            // Populate the exclusion set with all sub-dependencies
            for (const auto& sys_pkg : sys_subgraph) {
                sys_subgraph_names.insert(sys_pkg[pkg::NAME].get<std::string>());
            }
            
            // If subgraph_order doesn't include the root node, this prevents it from leaking into required_mounts.
            sys_subgraph_names.insert(current_dep);
            
            break; // Stop searching once we've found our top-level system package
        }
    }

    // 3. Reverse the full dependencies (as you originally wanted for mount ordering)
    std::reverse(full_deps.begin(), full_deps.end());

    // 4. Iterate and populate required mounts
    for (const auto& pkg : full_deps) {
        std::string current_dep = pkg[pkg::NAME].get<std::string>();
        std::string current_path = pkg[pkg::PATH].get<std::string>();

        // Skip the target package itself
        if (current_dep == pkg_name) {
            continue;
        }

        // Check if this package is inside the system package's subgraph exclusion set
        if (sys_subgraph_names.find(current_dep) == sys_subgraph_names.end()) {
            // It's NOT part of the system tree, so add it to required mounts
            required_mounts_vec.push_back(current_path);
        }
    }

    // 5. Construct the final JSON object
    json instr;
    instr[recipe::MOUNT_REQ] = required_mounts_vec; // Automatically converts to [] if empty
    instr[recipe::MOUNT_SYS] = sys_dep_path.empty() ? json::array() : json::array({sys_dep_path});
    
    return instr;
}