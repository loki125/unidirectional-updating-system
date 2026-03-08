#include "PackageReader.hpp"

std::unique_ptr<PackageReader> PackageReader::create(const std::string& type) {      
    if (type == "Debian") 
        return std::make_unique<DebReader>();
        
    throw std::runtime_error("Unsupported package type: " + type);
}

void DebReader::cleanup_processing_dirs(const std::vector<std::string>& target_packages) {
    for (const auto& pkg : target_packages) {
        fs::path pkg_path(pkg);
        fs::path processing_dir = pkg_path.parent_path() / "processing";
        
        if (fs::exists(processing_dir)) {
            // Recursively deletes the processing directory and all its contents
            fs::remove_all(processing_dir); 
        }
    }
}

std::string DebReader::extract_deb_to_processing(const std::string& deb_path) {
    fs::path pkg_path(deb_path);
    
    // Create the path: hash_path/processing/
    fs::path processing_dir = pkg_path.parent_path() / "processing";

    // Only extract if the processing directory doesn't already exist
    if (!fs::exists(processing_dir)) {
        fs::create_directories(processing_dir);
        
        // Extract the .deb file into the processing directory
        // using dpkg-deb -x <deb_file> <target_dir>
        std::string cmd = "dpkg-deb -x " + pkg_path.string() + " " + processing_dir.string();
        exec_command(cmd);
    }

    return processing_dir.string();
}

std::map<std::string, json> DebReader::generate_forests(const std::vector<std::string>& target_packages){

    // build_provider_map will extract packages if they haven't been extracted yet
    const std::map<std::string, std::string>& global_provider_map = build_provider_map(target_packages);
    std::map<std::string, json> results;

    for (const auto& pkg : target_packages) {
        std::map<std::string, std::string> pkg_forest;
        
        if (fs::exists(pkg)) {
            // Get the processing directory instead of iterating over the .deb file directly
            std::string processing_dir = extract_deb_to_processing(pkg);

            // Iterate over the EXTRACTED processing directory
            for (const auto& entry : fs::recursive_directory_iterator(processing_dir)) {
                if (entry.is_regular_file()) {
                    // Check for NEEDED libraries in the binary
                    std::string cmd = "readelf -d " + entry.path().string() + " 2>/dev/null | grep NEEDED";
                    std::string needed_libs_raw = exec_command(cmd);
                    
                    if (!needed_libs_raw.empty()) {
                        std::regex lib_regex("\\[(.*?)\\]");
                        auto words_begin = std::sregex_iterator(needed_libs_raw.begin(), needed_libs_raw.end(), lib_regex);
                        auto words_end = std::sregex_iterator();

                        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
                            std::string lib_needed = (*i)[1].str();
                            
                            if (global_provider_map.count(lib_needed)) {
                                pkg_forest[lib_needed] = global_provider_map.at(lib_needed);
                            }
                        }
                    }
                }
            }
        }

        // Return a JSON object for this package containing the "symlink_forest" key
        results[pkg] = json{
            {"symlink_forest", pkg_forest}
        };
    }

    //Delete all the extracted processing directories before finishing
    this->cleanup_processing_dirs(target_packages);
    return results;
}

// Build a map of every .so and its SONAME to its location in the store
std::map<std::string, std::string> DebReader::build_provider_map(const std::vector<std::string>& all_store_paths) {
    std::map<std::string, std::string> provider_map;
    
    for (const auto& pkg : all_store_paths) {
        if (!fs::exists(pkg)) continue;
        
        // Extract the package and get the processing directory
        std::string processing_dir = extract_deb_to_processing(pkg);
        fs::path proc_dir_path(processing_dir), pkg_path(pkg);
        
        // Iterate over the EXTRACTED processing directory
        for (const auto& entry : fs::recursive_directory_iterator(processing_dir)) {
            if (entry.is_regular_file() && (entry.path().extension() == ".so" || entry.path().string().find(".so.") != std::string::npos)) {
                
                std::string soname = get_elf_tag(entry.path().string(), "SONAME");
                
                // If no SONAME, use the filename as the lookup key
                std::string key = soname.empty() ? entry.path().filename().string() : soname;
                
                // skipping "processing" and volume directories in the path to get the correct relative path inside the store
                fs::path relative_inside_deb = entry.path().lexically_relative(proc_dir_path);
                fs::path hash_name = proc_dir_path.parent_path().filename();

                fs::path final_path = hash_name / relative_inside_deb;
                
                // Map the library name to the cleaned up path
                provider_map[key] = final_path.string();
            }
        }
    }
    return provider_map;
}

// Helper to extract ELF tags (like SONAME or NEEDED)
std::string DebReader::get_elf_tag(const std::string& path, const std::string& tag) {
    std::string cmd = "readelf -d " + path + " 2>/dev/null | grep " + tag;
    std::string output = exec_command(cmd);
    std::regex reg("\\[(.*?)\\]");
    std::smatch match;
    if (std::regex_search(output, match, reg)) {
        return match[1].str();
    }
    return "";
}

json DebReader::get_scripts(const std::string &path){
    json scripts;
    std::vector<std::string> pre, in_ov;
    std::string out = exec_command("dpkg-deb -I " + path);
    
    if (out.find(" preinst ") != std::string::npos) pre.push_back("preinst");
    if (out.find(" postinst ") != std::string::npos) in_ov.push_back("postinst");

    scripts["pre_overlay"] = pre;
    scripts["in_overlay"] = in_ov;
    return scripts;
}


fs::path DebReader::get_pkg_path(const fs::path &directory_path){
        // Find the package file (agnostic search)
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
