#include "UpdateBuilder.hpp"

fs::path UpdateBuilder::build(
    PackageService *service, 
    const PackageMetadata &metadata, 
    const std::string &f_file_path, 
    const fs::path& broadcaster_path)
{

    std::vector<PackageMetadata> packages_for_manifest;
    std::vector<std::string> file_list;
    constraint_map cycle_breaking_constraints;
    long long total_size_byte = 0;

    bool graph_resolved = false;
    std::unique_ptr<GSO> global_sort;

    while (!graph_resolved) {
        packages_for_manifest.clear();
        file_list.clear();
        total_size_byte = 0;

        std::vector<PackageMetadata> all_resolved_packages = service->get_recursive_dependencies(
            metadata, 
            f_file_path,
            cycle_breaking_constraints
        );

        for (const auto& dep_meta : all_resolved_packages) {
            std::string dep_file_path = service->download_path / dep_meta.Filename; 
            
            if (std::find(file_list.begin(), file_list.end(), dep_file_path) == file_list.end()) {
                file_list.push_back(dep_file_path);
                packages_for_manifest.push_back(dep_meta);
                total_size_byte += dep_meta.Size;
            }
        }

        try {
            global_sort = std::make_unique<GSO>(packages_for_manifest);
            
            graph_resolved = true; 

        } catch (const HardConflictException& e) {
            spdlog::warn("Cycle blocked resolution! Banning {} == {}. Retrying resolver...", e.pkg_name, e.pkg_version);        
            cycle_breaking_constraints[e.pkg_name].push_back({DpkgOps::LT, e.pkg_version});
        }
    }
    std::vector<PackageMetadata> sorted_pkgs_for_manifest = global_sort->get_sorted_pkgs();

    provider_map global_provider_map = service->build_provider_map(sorted_pkgs_for_manifest);
    forest_map forests = service->generate_forests(global_provider_map, *global_sort);

    for(auto& pkg_meta : sorted_pkgs_for_manifest) {

        json recipe = this->_generate_recipe(
            pkg_meta, 
            service, 
            global_provider_map[pkg_meta.Store_Path], 
            forests[pkg_meta.Store_Path],
            *global_sort
        );

        pkg_meta.Recipe = recipe.dump();
    }

    UpdateManifest manifest = this->_build_update_manifest(metadata, sorted_pkgs_for_manifest, total_size_byte);
    return this->_create_tar_object(manifest, file_list, broadcaster_path);
}

fs::path UpdateBuilder::_create_tar_object(const UpdateManifest& manifest, 
                                           const std::vector<std::string>& file_paths, 
                                           const fs::path& tar_path) 
{
    fs::path new_file_path = tar_path / (manifest.update_id + ".tar");
    struct archive *tar = archive_write_new();
    
    archive_write_set_format_pax_restricted(tar); 
    if (archive_write_open_filename(tar, new_file_path.string().c_str()) != ARCHIVE_OK) {
        archive_write_free(tar);
        throw std::runtime_error("Failed to open tar file for writing: " + new_file_path.string());
    }

    if (!manifest.packages.empty()) {
        json manifest_json = manifest.to_json();
        spdlog::debug("Generated manifest JSON: {}", manifest_json.dump());
        std::string manifest_data = manifest_json.dump();
        
        struct archive_entry *entry = archive_entry_new();
        archive_entry_set_pathname(entry, "manifest.json");
        archive_entry_set_size(entry, manifest_data.size());
        archive_entry_set_filetype(entry, AE_IFREG); // Regular file
        archive_entry_set_perm(entry, 0644);         // Standard permissions
        
        archive_write_header(tar, entry);
        archive_write_data(tar, manifest_data.c_str(), manifest_data.size());
        
        archive_entry_free(entry);
    }

    for (const std::string& path : file_paths) {
        if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
            archive_write_close(tar);
            archive_write_free(tar);
            throw std::runtime_error("Missing component file: " + path);
        }

        struct archive_entry *file_entry = archive_entry_new();
        
        std::string base_name = std::filesystem::path(path).filename().string();
        archive_entry_set_pathname(file_entry, base_name.c_str());
        
        size_t file_size = std::filesystem::file_size(path);
        archive_entry_set_size(file_entry, file_size);
        archive_entry_set_filetype(file_entry, AE_IFREG);
        archive_entry_set_perm(file_entry, 0644);
        
        archive_write_header(tar, file_entry);

        std::ifstream ifs(path, std::ios::binary);
        char buff[8192];
        while (ifs.read(buff, sizeof(buff))) {
            archive_write_data(tar, buff, ifs.gcount());
        }
        if (ifs.gcount() > 0) {
            archive_write_data(tar, buff, ifs.gcount());
        }

        archive_entry_free(file_entry);
    }

    archive_write_close(tar);
    archive_write_free(tar);

    return new_file_path;
}

json UpdateBuilder::_calculate_mounts(const std::string &pkg_name, const std::string &pkg_version, PackageService *service, const GSO& global_sort)
{
    std::vector<std::string> required_mounts_vec;
    std::string sys_dep_path;

    auto full_deps = global_sort.subgraph_order(pkg_name, pkg_version);
    
    std::unordered_set<std::string> sys_subgraph_names;

    for (auto it = full_deps.rbegin(); it != full_deps.rend(); ++it) {
        const auto& pkg = *it;
        std::string current_dep = pkg.Package;
        
        if (current_dep == pkg_name) {
            continue; 
        }

        if (service->is_system_pkg(current_dep)) {
            sys_dep_path = pkg.Store_Path;
            
            std::string sys_ver = pkg.Version; 
            auto sys_subgraph = global_sort.subgraph_order(current_dep, sys_ver);
            
            for (const auto& sys_pkg : sys_subgraph) {
                sys_subgraph_names.insert(sys_pkg.Package);
            }
            
            sys_subgraph_names.insert(current_dep);
            
            break; 
        }
    }

    std::reverse(full_deps.begin(), full_deps.end());

    for (const auto& pkg : full_deps) {
        std::string current_dep = pkg.Package;
        std::string current_path = pkg.Store_Path;

        if (current_dep == pkg_name) {
            continue;
        }

        if (sys_subgraph_names.find(current_dep) == sys_subgraph_names.end()) {
            required_mounts_vec.push_back(current_path);
        }
    }

    json instr;
    instr[recipe::MOUNT_REQ] = required_mounts_vec; 
    instr[recipe::MOUNT_SYS] = sys_dep_path.empty() ? json::array() : json::array({sys_dep_path});
    
    return instr;
}

UpdateManifest UpdateBuilder::_build_update_manifest(const PackageMetadata &metadata, const std::vector<PackageMetadata> &packages, size_t total_size_byte)
{
    UpdateManifest manifest;
    manifest.update_id = metadata.generate_id();
    manifest.pkgs_type = metadata.Type;
    manifest.format_version = "1.0"; 
    manifest.total_size_byte = total_size_byte;
    manifest.packages = packages;

    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&now_time), "%Y-%m-%dT%H:%M:%SZ");
    manifest.timestamp = ss.str();

    return manifest;
}

json UpdateBuilder::_generate_recipe(
    PackageMetadata& metadata, 
    PackageService* service, 
    const provider_vector &provider_vector, 
    const json &forest, 
    const GSO& global_sort)
{
    json recipe;
    
    bool is_system = service->is_system_pkg(metadata.Package);
    recipe[recipe::IS_SYSTEM] = is_system;
    
    recipe[recipe::PACKAGE_NAME] = metadata.Package;
    recipe[recipe::VERSION] = metadata.Version;

    json mount_instr = this->_calculate_mounts(metadata.Package, metadata.Version, service, global_sort);
    recipe[recipe::MOUNT_INS] = mount_instr.empty() ? json::array() : mount_instr;

    
    recipe[recipe::SYMLINK_FOREST] = forest;

    std::vector<std::string> temp_list;
    temp_list.reserve(provider_vector.size()); 
    for (const auto& [name, soname, is_exe] : provider_vector) {
        if(is_exe)
            temp_list.push_back(name);
    }
    recipe[recipe::PROVIDER_MAP] = temp_list; 
        
    recipe[recipe::STATUS] = service->get_status(metadata.Filename);
    spdlog::debug("Recipe generated successfully for {} \n{}", metadata.Package, recipe.dump(4));

    return recipe;
}
