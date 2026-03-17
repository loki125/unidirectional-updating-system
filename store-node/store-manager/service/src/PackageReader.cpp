#include "PackageReader.hpp"


std::unique_ptr<PackageReader> PackageReader::create(const std::string& type) {      
    if (type == "Debian") 
        return std::make_unique<DebReader>();
        
    throw std::runtime_error("Unsupported package type: " + type);
}

forest_map DebReader::generate_forests(const std::vector<std::string>& target_paths, const provider_map& global_provider_map, const GSO& global_sort) {

    forest_map results;

    const std::vector<json>& sorted_pkgs = global_sort.get_sorted_pkgs();
    for (const auto& pkg : sorted_pkgs) {
        const auto name = pkg[pkg::NAME].get<std::string>();
        const auto version = pkg[pkg::VERSION].get<std::string>();
        
        const auto store_path = pkg[pkg::PATH].get<fs::path>();

        for (const json& dec_pkg : global_sort.subgraph_order(name, version)) {
            fs::path dec_path = dec_pkg[pkg::PATH].get<fs::path>();
            auto provider_it = global_provider_map.find(dec_path);

            if (dec_path == store_path || provider_it == global_provider_map.end()) 
                continue; // skip self or if no providers found
                
            spdlog::info("For package {}-{}, checking dependencies in subgraph order: {}", name, version, dec_pkg[pkg::NAME].get<std::string>());

            for(auto& [provided_name, provided_soname] : provider_it->second) {
                results[store_path].insert({
                    provided_soname, 
                    dec_path / provided_name
                });
            }
        }
    }

    // No processing directories to clean up anymore!
    return results;
}

bool DebReader::is_system_pkg(const std::string& pkg_name) {
    // Core system libraries that the OS / Bootstrapper provides
    if (pkg_name == "libc6") 
        return true;
    
    // Anything related to character set conversion (the huge gconv list)
    if (pkg_name.find("libc-gconv") != std::string::npos) 
        return true;
    
    //other heavy system packages that bloat the forest
    if (pkg_name == "libgcc-s1")   
        return true;
    
    return false;
}

// Build a map of every .so and its SONAME to its location in the store using LibArchive streams
provider_map DebReader::build_provider_map(const std::vector<std::string>& all_store_paths) {
    provider_map p_map;
    
    for (const auto& pkg : all_store_paths) {
        if (!fs::exists(pkg)) continue;

        const std::string pkg_name = this->get_name(pkg);
        if (this->is_system_pkg(pkg_name)) {
            spdlog::info("Skipping system package: {}", pkg_name);
            continue; 
        }
        // Use the package's true path as the unique key
        fs::path map_key = fs::path(pkg).parent_path().filename(); // Use the directory name as the key (which should be unique due to store structure)
        
        // Output the raw tar stream from dpkg-deb and pipe it directly to libarchive
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
            // We only care about regular files
            if (archive_entry_filetype(entry) != AE_IFREG) continue;
            
            std::string path_str = archive_entry_pathname(entry);
            
            // Clean up leading "./" standard in tar files
            if (path_str.length() >= 2 && path_str.substr(0, 2) == "./") {
                path_str = path_str.substr(2); 
            }
            
            fs::path path(path_str);
            bool is_so = (path.extension() == ".so" || path_str.find(".so.") != std::string::npos);
            bool is_executable = (archive_entry_mode(entry) & 0111); // Checks for +x permission
            
            if (is_so || is_executable) {
                // Create a secure, unique temporary file in /tmp
                char tmp_template[] = "/tmp/lib_XXXXXX";
                int fd = mkstemp(tmp_template);
                
                if (fd != -1) {
                    // Extract exactly this single file to the secure temp location
                    archive_read_data_into_fd(a, fd);
                    close(fd);
                    
                    if (is_so) {
                        // Pass the temporary file path to your ELF reader
                        std::vector<std::string> sonames = this->get_elf_tags(tmp_template, "SONAME");

                        std::string soname = path.parent_path().string() + "/" + (sonames.empty() ? path.filename().string() : sonames[0]);
                        std::string key = path.parent_path().string() + "/" + path.filename().string();

                        p_map[map_key].push_back(std::make_pair(key, soname));
                    } 
                    else if (is_executable) {
                        p_map[map_key].push_back(std::make_pair(path_str, path_str));
                    }
                    
                    // Immediately delete the file from disk
                    unlink(tmp_template); 
                }
            }
        }
        
        // Clean up the archive and pipe handlers
        archive_read_free(a);
        pclose(pipe);
    }
    
    return p_map;
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