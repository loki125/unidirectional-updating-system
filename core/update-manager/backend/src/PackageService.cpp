#include "PackageService.hpp"

void DebianPackageService::init() {
    client = std::make_unique<httplib::Client>(BASE_URL);

    // onfigure Timeouts
    client->set_connection_timeout(CONNECTION_TIMEOUT, 0); 
    client->set_read_timeout(READ_TIMEOUT, 0);

    // Follow Redirects
    client->set_follow_location(true);
    spdlog::info("DebianPackageService initialized for {}", BASE_URL);
}

void DebianPackageService::cleanup() {
    if (client) {
        spdlog::info("Cleaning up DebianPackageService resources...");
        client.reset(); // effectively "closes" the session
    }
}

json DebianPackageService::_get_json(const std::string& endpoint) {
    if (!client) {
        spdlog::error("HTTP client not initialized. Call init() first.");
        return nullptr;
    }

    auto res = client->Get(endpoint);

    if (res && res->status == 200) {
        try {
            return json::parse(res->body);
        } catch (const json::parse_error& e) {
            spdlog::error("Failed to parse JSON from {}: {}", endpoint, e.what());
        }
    } else {
        int status = res ? res->status : -1;
        spdlog::error("API Error at {}: Status {}", endpoint, status);
    }

    return nullptr;
}

std::string DebianPackageService::_calculate_sha256(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) return "";

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);

    std::vector<char> buffer(BUFFER_SIZE);
    while (file.read(buffer.data(), buffer.size()) || file.gcount()) {
        SHA256_Update(&sha256, buffer.data(), file.gcount());
    }

    SHA256_Final(hash, &sha256);

    std::stringstream ss;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

std::vector<std::vector<std::string>> DebianPackageService::_resolve_dependencies(const std::string& depends_str, 
                                                                                const std::string& target_arch) {
    if (depends_str.empty()) return {};

    // 1. Parse the raw string into package names and their constraints
    auto package_constraints = _parse_constraints(depends_str);
    std::vector<std::vector<std::string>> resolved_list;

    for (const auto& [pkg_name, constraints] : package_constraints) {
        // 2. Find the best version that fits all constraints
        std::string best_version = _find_best_version(pkg_name, constraints);
        
        if (!best_version.empty()) {
            // 3. Verify architecture (target_arch or 'all')
            std::string effective_arch = _target_arch_or_all(pkg_name, best_version, target_arch);
            
            if (!effective_arch.empty()) {
                resolved_list.push_back({pkg_name, best_version, effective_arch});
            }
        } else {
            spdlog::warn("Could not resolve version for package: {} with given constraints", pkg_name);
        }
    }

    return resolved_list;
}

std::vector<json> DebianPackageService::get_package_instances(const std::string& pkg_name) {
    std::string endpoint = "/mr/binary/" + pkg_name + "/";
    json data = _get_json(endpoint);

    if (data.is_null() || !data.contains("result")) {
        return {};
    }

    return data["result"].get<std::vector<json>>();
}

json DebianPackageService::get_package_info(const std::string& pkg_name, 
                                                     const std::string& version, 
                                                     const std::string& architecture) {
    // URL encode the version (handling characters like : and +)
    std::string encoded_version = encode_url(version);
    std::string endpoint = "/mr/binary/" + pkg_name + "/" + encoded_version + "/binfiles?fileinfo=1";
    
    json data = _get_json(endpoint);
    if (data.is_null()) {
        spdlog::error("Package: {}_{}_{} not found", pkg_name, version, architecture);
        return nullptr;
    }

    auto arch_list = data.value("result", json::array());
    auto file_info_map = data.value("fileinfo", json::object());
    std::string target_hash = "";

    for (const auto& entry : arch_list) {
        if (entry.value("architecture", "") == architecture) {
            target_hash = entry.value("hash", "");
            break;
        }
    }

    if (target_hash.empty()) {
        spdlog::error("Package {}_{} has no support for architecture {}", pkg_name, version, architecture);
        return nullptr;
    }

    // Snapshot API structure: fileinfo[hash] is an array, we take the first element
    if (file_info_map.contains(target_hash) && !file_info_map[target_hash].empty()) {
        json file_info = file_info_map[target_hash][0];
        file_info["SHA1"] = target_hash; // Attach the hash as in Python code
        return file_info;
    }

    return nullptr;
}


PackageMetadata DebianPackageService::get_package_metadata(const std::string& file_path) {
    // Get raw control fields (calls dpkg-deb internally)
    auto control_data = _get_raw_control_data(file_path);

    // Calculate SHA256 of the local file
    std::string sha256 = _calculate_sha256(file_path);

    std::string arch = control_data["Architecture"];
    
    // Combine Depends and Pre-Depends
    std::string combined_depends = control_data["Depends"];
    if (!control_data["Pre-Depends"].empty()) {
        if (!combined_depends.empty()) combined_depends += ", ";
        combined_depends += control_data["Pre-Depends"];
    }

    // Resolve dependencies into structured format
    auto resolved_deps = _resolve_dependencies(combined_depends, arch);

    // Construct the Struct
    PackageMetadata metadata;
    metadata.Package = control_data["Package"];
    metadata.Version = control_data["Version"];
    metadata.Type = "Debian"; // Hardcoded for this service
    metadata.Architecture = arch;
    metadata.Dependencies = resolved_deps;
    metadata.SHA256 = sha256;
    metadata.compute_store_path(); // Sets Store_Path based on SHA256, Package, and Version
    metadata.Installed_Size = control_data.count("Installed-Size") ? std::stoll(control_data["Installed-Size"]) : 0;
    
    metadata.Size = std::filesystem::file_size(file_path);
    metadata.Filename = std::filesystem::path(file_path).filename().string();

    return metadata;
}

std::string DebianPackageService::_target_arch_or_all(const std::string& pkg_name, 
                                                      const std::string& version, 
                                                      const std::string& target_arch) {
    std::string encoded_ver = encode_url(version);
    std::string endpoint = "/mr/binary/" + pkg_name + "/" + encoded_ver + "/binfiles";
    
    json data = _get_json(endpoint);
    if (data.is_null() || !data.contains("result")) {
        return "";
    }

    bool has_all = false;
    auto results = data["result"];

    for (const auto& entry : results) {
        std::string arch = entry.value("architecture", "");
        
        // If we find an exact match, we're done
        if (arch == target_arch) {
            return target_arch;
        }
        
        if (arch == "all") {
            has_all = true;
        }
    }

    // Fallback to 'all' if the specific arch wasn't found
    return has_all ? "all" : "";
}

bool DebianPackageService::_compare_versions(const std::string& v1, 
                                            const std::string& op, 
                                            const std::string& v2) {
    static const std::map<std::string, std::string> op_map = {
        {"<<", "lt"}, {"<=", "le"}, {"=", "eq"}, 
        {"!=", "ne"}, {">=", "ge"}, {">>", "gt"}
    };

    std::string dpkg_op = op_map.count(op) ? op_map.at(op) : "eq";
    std::string cmd = "dpkg --compare-versions '" + v1 + "' " + dpkg_op + " '" + v2 + "'";

    int exit_code = execute_status_cmd(cmd);
    return exit_code == 0;
}

std::map<std::string, std::string> DebianPackageService::_get_raw_control_data(const std::string& file_path) {
    if (!std::filesystem::exists(file_path)) {
        throw std::runtime_error("File not found: " + file_path);
    }

    // dpkg-deb -I <file> extracts the control info to stdout
    CommandResult res = execute_command("dpkg-deb -I '" + file_path + "'");

    if (res.exit_code != 0) {
        throw std::runtime_error("dpkg-deb error: " + res.stderr_res);
    }

    std::map<std::string, std::string> control_data;
    std::stringstream ss(res.stdout_res);
    std::string line;

    while (std::getline(ss, line)) {
        // dpkg-deb -I output lines usually start with spaces/tabs
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);

            // Trim leading/trailing whitespace
            key.erase(0, key.find_first_not_of(" \t\r\n"));
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            value.erase(0, value.find_first_not_of(" \t\r\n"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);

            if (!key.empty()) {
                control_data[key] = value;
            }
        }
    }

    return control_data;
}

std::string DebianPackageService::_find_best_version(
    const std::string& pkg_name, 
    const std::vector<std::pair<std::string, std::string>>& constraints) 
{
    // Queries Snapshot for all versions
    std::vector<json> instances = get_package_instances(pkg_name);
    if (instances.empty()) {
        return "";
    }

    std::vector<std::string> all_versions;
    all_versions.reserve(instances.size());
    
    for (const auto& i : instances) {
        if (i.contains("version") && i["version"].is_string()) {
            all_versions.push_back(i["version"].get<std::string>());
        }
    }

    if (all_versions.empty()) {
        return "";
    }

    // If no constraints at all, return the newest version
    if (constraints.empty()) {
        return all_versions.front();
    }

    // Check every available version against ALL gathered constraints for this package
    for (const std::string& v : all_versions) {
        bool is_valid = true;
        
        for (const auto& constraint : constraints) {
            const std::string& op = constraint.first;
            const std::string& target_ver = constraint.second;

            if (!_compare_versions(v, op, target_ver)) {
                is_valid = false;
                break; // Failed one constraint, move to the next version
            }
        }

        if (is_valid) {
            // found the highest (first) version that satisfies all checks, we can return 
            return v;
        }
    }

    // If nothing satisfies the constraints, we can't resolve it.
    return ""; 
}

std::map<std::string, std::vector<std::pair<std::string, std::string>>> 
DebianPackageService::_parse_constraints(const std::string& depends_str) 
{
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> package_constraints;
    
    if (depends_str.empty()) {
        return package_constraints;
    }

    // Compile regex once using 'static const' for performance.
    static const std::regex dep_regex(R"(^([a-z0-9\+\-\.]+)(:([a-z0-9]+))?(\s*\((<<|<=|=|>=|>>)\s*([^)]+)\))?)");

    std::stringstream ss(depends_str);
    std::string part;

    // Split by ','
    while (std::getline(ss, part, ',')) {
        // Split by '|' and take the first preferred dependency
        size_t pipe_pos = part.find('|');
        std::string preferred_dep = (pipe_pos != std::string::npos) ? part.substr(0, pipe_pos) : part;

        // Strip leading and trailing whitespace
        size_t start = preferred_dep.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue; // Skip if it's all whitespace

        size_t end = preferred_dep.find_last_not_of(" \t\r\n");
        preferred_dep = preferred_dep.substr(start, end - start + 1);

        std::smatch match;
        if (!std::regex_search(preferred_dep, match, dep_regex)) {
            continue;
        }

        // match[1] = pkg_name, match[5] = operator, match[6] = version
        std::string pkg_name = match[1].str();
        package_constraints[pkg_name];

        // Check if the operator and version capture groups actually matched anything
        if (match[5].matched && match[6].matched) {
            std::string constraint_op = match[5].str();
            std::string constraint_ver = match[6].str();
            package_constraints[pkg_name].push_back({constraint_op, constraint_ver});
        }
    }

    return package_constraints;
}

std::string DebianPackageService::_calculate_sha1(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    SHA_CTX sha1_ctx;
    SHA1_Init(&sha1_ctx);

    char buffer[64 * 1024]; // 64KB buffer
    while (file.read(buffer, sizeof(buffer))) {
        SHA1_Update(&sha1_ctx, buffer, file.gcount());
    }
    // Process the last partial chunk if any
    if (file.gcount() > 0) {
        SHA1_Update(&sha1_ctx, buffer, file.gcount());
    }

    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1_Final(hash, &sha1_ctx);

    std::stringstream ss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    
    return ss.str();
}

std::string DebianPackageService::get_package_file(const std::string& pkg_name, 
                                                   const std::string& version, 
                                                   const std::string& architecture) 
{
    // Get binary metadata
    json pkg_meta = get_package_info(pkg_name, version, architecture);
    if (pkg_meta.empty() || pkg_meta.is_null()) {
        throw std::runtime_error("No binary found for " + pkg_name + " " + version + " (" + architecture + ")");
    }

    std::string expected_sha1 = pkg_meta.value("SHA1", "");
    std::string file_name = pkg_meta.value("name", "");
    std::string first_seen = pkg_meta.value("first_seen", "");
    std::string path_str = pkg_meta.value("path", "");

    if (file_name.empty()) {
        throw std::runtime_error("Missing file name in metadata for " + pkg_name);
    }

    std::string endpoint = "/archive/debian/" + first_seen + "/" + path_str + "/" + encode_url(file_name);

    httplib::Result res;
    bool success = false;

    for (int try_count = 0; try_count <= MAX_DOWNLOAD_RETRIES; ++try_count) {
        std::ofstream outfile(file_name, std::ios::binary | std::ios::trunc);
        
        if (!outfile.is_open()) {
            throw std::runtime_error("Failed to open local file: " + file_name);
        }

        res = client->Get(endpoint.c_str(), [&](const char *data, size_t data_length) {
            outfile.write(data, data_length);
            return outfile.good(); // Stop if disk is full or file becomes unwritable
        });

        outfile.close();

        if (res && res->status == 200) {
            success = true;
            break; 
        }

        bool is_network_error = !res;
        bool is_server_error = res && res->status >= 500;

        if (is_network_error || is_server_error) {
            spdlog::warn("Attempt {} failed for {}. Retrying in 2s...", try_count + 1, file_name);
            std::this_thread::sleep_for(std::chrono::seconds(2));
        } else {
            // It's a 404, 403, or 401. It won't fix itself by retrying.
            break; 
        }
    }

    if (!success) {
        std::remove(file_name.c_str()); // Clean up
        std::string err_msg = "Final download failure for " + file_name + ": ";
        err_msg += res ? std::to_string(res->status) : httplib::to_string(res.error());
        throw std::runtime_error(err_msg);
    }

    // Perform Integrity Check using our new helper!
    std::string actual_sha1 = _calculate_sha1(file_name);
    
    if (!expected_sha1.empty() && actual_sha1 != expected_sha1) {
        std::remove(file_name.c_str());
        throw std::runtime_error(
            "Integrity check failed for " + file_name + "!\n"
            "Expected: " + expected_sha1 + "\n"
            "Actual:   " + actual_sha1
        );
    }

    spdlog::info("Downloaded and verified: {}", file_name);
    return file_name;
}

std::vector<PackageMetadata> DebianPackageService::get_recursive_dependencies(
    const PackageMetadata& metadata, 
    const std::string& file_path) 
{
    // The target architecture we are resolving dependencies for.
    std::string arch = metadata.Architecture;
    
    // global_constraints: Maps a package name to ALL of the version requirements 
    // we have discovered for it so far across the entire tree.
    // Example: "libc6" -> { (">=", "2.28"), ("<<", "3.0") }
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> global_constraints;
    
    // resolved_packages: Maps package name to its full PackageMetadata. 
    // Acts as our local cache so we don't re-download or re-parse known packages.
    std::map<std::string, PackageMetadata> resolved_packages;
    
    // replaces_provides: Tracks packages that are obsolete because another package 
    // we are downloading 'Replaces' or 'Provides' them.
    std::set<std::string> replaces_provides;
    
    // enqueued: A quick-lookup set to prevent adding the same package to the queue repeatedly.
    std::set<std::string> enqueued;
    
    // queue: The actual processing queue for breadth-first dependency resolution.
    std::queue<std::string> queue;
    
    // dependency_edges: Stores relationships as (ParentPackage, ChildPackage).
    // We record these edges flatly during resolution, and build the actual 
    // hierarchical tree at the very end.
    std::vector<std::pair<std::string, std::string>> dependency_edges;

    // LAMBDA: check_and_queue
    // Evaluates constraints for a specific package requirement. It decides if 
    // the package needs to be downloaded, or RE-downloaded if rules got stricter.
    auto check_and_queue = [&](const std::string& pkg, 
                               const std::vector<std::pair<std::string, std::string>>& reqs, 
                               const PackageMetadata& parent_metadata, 
                               bool invert = false, 
                               bool is_essential_break = false) 
    {
        // 1. Check if it's a tool we explicitly want to ignore
        if (IGNORE_PACKAGES.count(pkg)) return;

        // Ensure the package exists in our global constraints map (creates empty vector if new)
        auto& current_constraints_for_pkg = global_constraints[pkg];
        
        std::vector<std::pair<std::string, std::string>> new_constraints;
        
        // 2. Process and potentially invert the requested operators
        for (const auto& req : reqs) {
            std::string op = req.first;
            std::string ver = req.second;
            
            // If this is a 'Breaks' or 'Conflicts', a rule like "Breaks < 13" 
            // actually means "Requires >= 13". We invert the operator.
            if (invert) {
                if (op == "<<") op = ">=";
                else if (op == "<=") op = ">>";
                else if (op == "=")  op = "!=";
                else if (op == ">=") op = "<<";
                else if (op == ">>") op = "<=";
            }
            
            new_constraints.push_back({op, ver});
            current_constraints_for_pkg.push_back({op, ver});
        }
        
        // 3. The "Middle Ground" Re-evaluation Rule
        // If we ALREADY downloaded this package, we must check if the version we 
        // picked satisfies these NEW, potentially stricter rules we just discovered.
        if (resolved_packages.count(pkg)) {
            std::string resolved_ver = resolved_packages[pkg].Version;
            for (const auto& req : new_constraints) {
                if (!_compare_versions(resolved_ver, req.first, req.second)) {
                    spdlog::warn("Version conflict! Have {} v{}, but need {} {}. Re-queueing to find middle ground.", 
                                 pkg, resolved_ver, req.first, req.second);
                    
                    // Push back into the queue. The next time it pops, _find_best_version 
                    // will look at ALL constraints combined and find a version that satisfies both.
                    queue.push(pkg);
                    break;
                }
            }
        }
        
        // 4. Queue Decision
        // We generally don't queue packages purely because they appeared in 'Breaks' (invert=True).
        // However, if an ESSENTIAL package breaks something, we MUST resolve it.
        bool should_queue = !invert || is_essential_break;
        
        if (should_queue) {
            // Record the graph edge: Parent requires Child
            dependency_edges.push_back({parent_metadata.Package, pkg});
            
            // Only add to the actual processing queue if it isn't already waiting in there
            if (!enqueued.count(pkg)) {
                enqueued.insert(pkg);
                queue.push(pkg);
            }
        }
    };

    // LAMBDA: process_control_data
    // Reads a dictionary of control file key-values and extracts dependency logic.
    auto process_control_data = [&](const std::map<std::string, std::string>& c_data, 
                                    const PackageMetadata& parent_metadata) 
    {
        // --- 1. Standard Dependencies (Depends + Pre-Depends) ---
        std::string d_str = c_data.count("Depends") ? c_data.at("Depends") : "";
        std::string pd_str = c_data.count("Pre-Depends") ? c_data.at("Pre-Depends") : "";
        
        // Safely combine them (avoiding stray commas)
        std::string combined_depends = d_str;
        if (!pd_str.empty()) {
            if (!combined_depends.empty()) combined_depends += ", ";
            combined_depends += pd_str;
        }

        auto parsed_d = _parse_constraints(combined_depends);
        for (const auto& [dep_pkg, reqs] : parsed_d) {
            check_and_queue(dep_pkg, reqs, parent_metadata, false, false);
        }

        // --- 2. Replaces / Provides ---
        // If a package replaces another, we add it to the 'replaces_provides' set
        // so we don't accidentally download the obsolete package later.
        if (c_data.count("Replaces")) {
            std::string rep_str = c_data.at("Replaces");
            std::stringstream ss(rep_str);
            std::string part;
            while (std::getline(ss, part, ',')) {
                size_t pipe_pos = part.find('|');
                std::string cleaned = (pipe_pos != std::string::npos) ? part.substr(0, pipe_pos) : part;
                
                // Trim leading whitespace
                size_t start = cleaned.find_first_not_of(" \t\r\n");
                if (start != std::string::npos) {
                    cleaned = cleaned.substr(start);
                    // Extract just the package name (before the first space)
                    size_t space_pos = cleaned.find(' ');
                    std::string r_pkg = (space_pos != std::string::npos) ? cleaned.substr(0, space_pos) : cleaned;
                    replaces_provides.insert(r_pkg);
                }
            }
        }

        // --- 3. Breaks / Conflicts ---
        if (c_data.count("Breaks")) {
            auto parsed_brk = _parse_constraints(c_data.at("Breaks"));
            for (const auto& [brk_pkg, reqs] : parsed_brk) {
                bool is_essential = ESSENTIAL_PACKAGES.count(brk_pkg) > 0;
                check_and_queue(brk_pkg, reqs, parent_metadata, true, is_essential);
            }
        }
    };

    // START MAIN RESOLUTION LOOP    
    // 1. Process the root package we were given
    std::map<std::string, std::string> root_control = _get_raw_control_data(file_path);
    enqueued.insert(metadata.Package);
    resolved_packages[metadata.Package] = metadata; // Add root to resolved so we don't redownload it
    
    process_control_data(root_control, metadata);

    // 2. Loop until the entire dependency tree is resolved
    while (!queue.empty()) {
        std::string current_pkg = queue.front();
        queue.pop();

        // If another package we downloaded declared it replaces this one, skip it.
        if (replaces_provides.count(current_pkg)) {
            continue;
        }

        // Get the combined constraints mapping, find the exact version that satisfies ALL
        std::vector<std::pair<std::string, std::string>> constraints = global_constraints[current_pkg];
        std::string best_version = _find_best_version(current_pkg, constraints);

        if (best_version.empty()) {
            spdlog::error("Unresolvable constraints! No version of {} satisfies the requirements.", current_pkg);
            continue;
        }

        // If we already downloaded this exact version, we are good. Move to next in queue.
        if (resolved_packages.count(current_pkg) && resolved_packages[current_pkg].Version == best_version) {
            continue;
        }

        // Check if the package supports the target arch, or if it's an 'all' arch package
        std::string target_arch = _target_arch_or_all(current_pkg, best_version, arch);
        if (target_arch.empty()) {
            spdlog::warn("Architecture {} not found for {}_{}", arch, current_pkg, best_version);
            continue;
        }

        // 3. Download the resolved package
        std::string dl_path;
        try {
            dl_path = get_package_file(current_pkg, best_version, target_arch);
        } catch (const std::exception& e) {
            spdlog::error("Failed to download {}: {}", current_pkg, e.what());
            continue;
        }

        // 4. Parse the newly downloaded package and update graph state
        PackageMetadata pkg_meta = get_package_metadata(dl_path);
        resolved_packages[current_pkg] = pkg_meta;

        std::map<std::string, std::string> curr_control = _get_raw_control_data(dl_path);
        
        // Process the new control data. This may discover MORE dependencies, 
        // adding them to the end of the queue.
        process_control_data(curr_control, pkg_meta);
    }

    // POST-PROCESSING: Link the Dependency Graph
    // Now that all packages are downloaded and we know their final versions, 
    // we iterate through the edges we recorded and build the tree structure 
    // inside the metadata objects.
    for (const auto& edge : dependency_edges) {
        const std::string& parent_name = edge.first;
        const std::string& child_name = edge.second;

        if (resolved_packages.count(parent_name) && resolved_packages.count(child_name)) {
            PackageMetadata& parent_meta = resolved_packages[parent_name];
            const PackageMetadata& child_meta = resolved_packages[child_name];
            
            // Check if it's already in the parent's Dependencies list to avoid duplicate entries
            bool exists = false;
            for (const auto& dep : parent_meta.Dependencies) {
                if (!dep.empty() && dep[0] == child_name) {
                    exists = true;
                    break;
                }
            }
            
            // Format: [Package, Version, Architecture]
            if (!exists) {
                parent_meta.Dependencies.push_back({
                    child_meta.Package, 
                    child_meta.Version, 
                    child_meta.Architecture
                });
            }
        }
    }

    // FINALIZE AND RETURN
    // We return a flat list of all resolved dependency metadata (excluding the original root package).
    // The hierarchy is preserved inside the 'Dependencies' vector of each PackageMetadata object.
    std::vector<PackageMetadata> final_metadata_list;
    final_metadata_list.reserve(resolved_packages.size() - 1);
    
    for (const auto& [pkg_name, meta] : resolved_packages) {
        if (pkg_name != metadata.Package) {
            final_metadata_list.push_back(meta);
        }
    }

    return final_metadata_list;
}