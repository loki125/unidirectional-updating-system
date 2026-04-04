#include "PackageReader.hpp"


std::unique_ptr<PackageReader> PackageReader::create(const std::string& type) {      
    if (type == "Debian") 
        return std::make_unique<DebReader>();
        
    throw std::runtime_error("Unsupported package type: " + type);
}

forest_map DebReader::generate_forests(const provider_map& global_provider_map, const GSO& global_sort) {

    forest_map results;
    const std::vector<json>& sorted_pkgs = global_sort.get_sorted_pkgs();
    
    for (const auto& pkg : sorted_pkgs) {
        const auto name = pkg[pkg::NAME].get<std::string>();
        const auto version = pkg[pkg::VERSION].get<std::string>();
        const auto store_path = pkg[pkg::PATH].get<fs::path>();

        auto full_deps = global_sort.subgraph_order(name, version);
        std::unordered_set<std::string> bundled_deps;
        
        // Identify system dependencies whose sub-dependencies are handled via bundling
        for (const auto& dep : full_deps) {
            std::string dep_name = dep[pkg::NAME].get<std::string>();
            if (this->is_system_pkg(dep_name) && dep_name != name) {
                auto sys_deps = global_sort.subgraph_order(dep_name, dep[pkg::VERSION].get<std::string>());
                for (const auto& sys_dep : sys_deps) {
                    std::string sys_dep_name = sys_dep[pkg::NAME].get<std::string>();
                    if (sys_dep_name != dep_name) {
                        bundled_deps.insert(sys_dep_name);
                    }
                }
            }
        }

        // Generate forest ignoring bundled dependencies
        for (const json& dec_pkg : full_deps) {
            std::string dec_name = dec_pkg[pkg::NAME].get<std::string>();
            
            // Skip checking subgraph items if they are bundled in a system package
            if (bundled_deps.find(dec_name) != bundled_deps.end()) {
                continue;
            }

            fs::path dec_path = dec_pkg[pkg::PATH].get<fs::path>();
            auto provider_it = global_provider_map.find(dec_path);

            if (dec_path == store_path || provider_it == global_provider_map.end()) 
                continue; 
                
            spdlog::info("For package {}-{}, checking dependencies in subgraph order: {}", name, version, dec_name);

            for(auto&[provided_name, provided_soname] : provider_it->second) {
                results[store_path].insert({
                    provided_soname, 
                    dec_path / provided_name
                });
            }
        }
    }

    return results;
}

bool DebReader::is_system_pkg(const std::string& pkg_name) {
    if (pkg_name == "libc6") return true;
    if (pkg_name.find("libc-gconv") != std::string::npos) return true;
    if (pkg_name == "libgcc-s1") return true;
    return false;
}

provider_map DebReader::build_provider_map(const std::vector<std::string>& all_store_paths) {
    provider_map p_map;

    for (const auto& pkg : all_store_paths) {
        if (!fs::exists(pkg)) continue;

        const std::string pkg_name = this->get_name(pkg);
        fs::path map_key = fs::path(pkg).parent_path().filename(); 
        
        std::string cmd = "dpkg-deb --fsys-tarfile " + pkg;
        FILE *pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            spdlog::error("Failed to run dpkg-deb stream on {}", pkg_name);
            continue;
        }
        
        struct archive *a = archive_read_new();
        archive_read_support_filter_all(a);
        archive_read_support_format_all(a);
        
        if (archive_read_open_FILE(a, pipe) != ARCHIVE_OK) {
            spdlog::error("Failed to open archive stream for {}", pkg_name);
            archive_read_free(a);
            pclose(pipe);
            continue;
        }
        
        struct archive_entry *entry;
        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            if (archive_entry_filetype(entry) != AE_IFREG) continue;
            
            std::string path_str = archive_entry_pathname(entry);
            if (path_str.length() >= 2 && path_str.substr(0, 2) == "./") {
                path_str = path_str.substr(2); 
            }
            
            fs::path path(path_str);
            bool is_so = (path.extension() == ".so" || path_str.find(".so.") != std::string::npos);
            bool is_executable = (archive_entry_mode(entry) & 0111);
            
            if (is_so || is_executable) {
                char tmp_template[] = "/tmp/lib_XXXXXX";
                int fd = mkstemp(tmp_template);
                
                if (fd != -1) {
                    archive_read_data_into_fd(a, fd);
                    close(fd);
                    
                    if (is_so) {
                        std::vector<std::string> sonames = this->get_elf_tags(tmp_template, "SONAME");
                        std::string soname = path.parent_path().string() + "/" + (sonames.empty() ? path.filename().string() : sonames[0]);
                        std::string key = path.parent_path().string() + "/" + path.filename().string();
                        p_map[map_key].push_back(std::make_pair(key, soname));
                    } 
                    else if (is_executable) {
                        p_map[map_key].push_back(std::make_pair(path_str, path_str));
                    }
                    
                    unlink(tmp_template); 
                }
            }
        }
        
        archive_read_free(a);
        pclose(pipe);
    }
    
    return p_map;
}

void DebReader::bundle_system_package(const std::string& sys_pkg_deb_path, const std::vector<json>& subgraph) {
    fs::path sys_deb(sys_pkg_deb_path);
    fs::path sys_store_path = sys_deb.parent_path();
    spdlog::info("Bundling dependencies into system package hash path at: {}", sys_store_path.parent_path().string());

    for (const auto& dep : subgraph) {
        fs::path dep_deb_path = dep[pkg::FILENAME].get<std::string>();
        
        // Skip the system package itself (don't link self to self)
        if (dep_deb_path == sys_deb) {
            continue;
        }

        spdlog::info("Hard-linking dependency {} into bundle", dep[pkg::NAME].get<std::string>());

        // Ensure the dependency directory actually exists
        if (!fs::exists(dep_deb_path) || !fs::is_regular_file(dep_deb_path)) {
            spdlog::warn("Dependency path {} missing or invalid. Skipping.", dep_deb_path.string());
            continue;
        }

        try {
            fs::path destination = sys_store_path / dep_deb_path.filename();

            // If the destination already exists, don't try to link again
            if (fs::exists(destination) || fs::is_symlink(destination)) {
                continue; 
            }

            if (fs::is_symlink(dep_deb_path)) {
                // Copy the symlink itself
                fs::copy_symlink(dep_deb_path, destination);
                spdlog::info("Copied symlink {} to {}", dep_deb_path.filename().string(), destination.string());
            } else {
                // Create a Hard Link: (Existing_File, New_Link)
                fs::create_hard_link(dep_deb_path, destination);
                spdlog::info("Hard-linked {} into bundle", dep_deb_path.filename().string());
            }
        } catch (const fs::filesystem_error& e) {
            spdlog::error("Failed to bundle dependency {}: {}", dep_deb_path.string(), e.what());
        }
    }
    spdlog::info("Successfully bundled all recursive dependencies into {}", sys_store_path.filename().string());
}

// Helper to extract ELF tags (like SONAME or NEEDED)
std::vector<std::string> DebReader::get_elf_tags(const std::string& path, const std::string& tag) {
    std::string cmd = "readelf -d " + path + " 2>/dev/null | grep " + tag;
    std::string output = exec_command(cmd);
    
    std::vector<std::string> results;
    static std::regex reg("\\[(.*?)\\]");
    
    auto words_begin = std::sregex_iterator(output.begin(), output.end(), reg);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        results.push_back((*i)[1].str());
    }
    
    return results;
}

json DebReader::get_scripts(const std::string &path){
    json scripts;
    std::vector<std::string> pre, in_ov;
    std::string out = exec_command("dpkg-deb -I " + path);
    
    if (out.find(std::string(" ") + script::PRE_INSTALL + " ") != std::string::npos) pre.push_back(script::PRE_INSTALL);
    if (out.find(std::string(" ") + script::POST_INSTALL + " ") != std::string::npos) in_ov.push_back(script::POST_INSTALL);

    scripts[script::PRE_OVERLAY] = pre;
    scripts[script::IN_OVERLAY] = in_ov;
    return scripts;
}

fs::path DebReader::get_pkg_path(const fs::path &directory_path){
    for (const auto& entry : fs::directory_iterator(directory_path)) {
        if (entry.path().extension() == ".deb") {
            return entry.path();
        }
    }
    throw std::runtime_error("No .deb package found in " + directory_path.string());
}

std::string DebReader::get_name(const std::string &path){
    std::string out = exec_command("dpkg-deb -f " + path + " Package");
    out.erase(out.find_last_not_of(" \n\r\t") + 1);
    return out;
}

std::string DebReader::get_version(const std::string &path){
    std::string out = exec_command("dpkg-deb -f " + path + " Version");
    out.erase(out.find_last_not_of(" \n\r\t") + 1);
    return out;
}