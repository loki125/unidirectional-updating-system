#include "PackageService.hpp"

void DebianPackageService::init() {
    client = std::make_unique<httplib::Client>(BASE_URL);

    client->set_connection_timeout(CONNECTION_TIMEOUT, 0); 
    client->set_read_timeout(READ_TIMEOUT, 0);

    client->set_follow_location(true);
    spdlog::info("DebianPackageService initialized for {}", BASE_URL);
}

void DebianPackageService::cleanup() noexcept{
    if (client) {
        spdlog::info("Cleaning up DebianPackageService resources...");
        client.reset(); 
    }
}

json DebianPackageService::_get_json(const std::string& endpoint) noexcept{
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

std::vector<std::string> DebianPackageService::get_elf_tags(const std::string &path, const std::string &tag)
{
    CommandResult cmd_result = execute_command("readelf -d " + path + " 2>/dev/null | grep " + tag);
    std::string output = cmd_result.stdout_res;
    
    std::vector<std::string> results;
    static std::regex reg("\\[(.*?)\\]");
    
    auto words_begin = std::sregex_iterator(output.begin(), output.end(), reg);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        results.push_back((*i)[1].str());
    }
    
    return results;
}

std::optional<std::string> DebianPackageService::extract_soname_from_archive(archive *a)
{
    char tmp_template[] = "/tmp/lib_XXXXXX";
    int fd = mkstemp(tmp_template);
    if (fd == -1) return std::nullopt;

    archive_read_data_into_fd(a, fd);
    close(fd);

    auto sonames = this->get_elf_tags(tmp_template, "SONAME");
    unlink(tmp_template);

    if (sonames.empty()) return std::nullopt;
    return sonames[0];
}

std::vector<Depend> DebianPackageService::_resolve_dependencies(const std::string& depends_str, const std::string& target_arch) {
    if (depends_str.empty()) return {};

    auto package_constraints = _parse_constraints(depends_str);
    std::vector<Depend> resolved_list;

    for (const auto& [pkg_name, constraints] : package_constraints) {
        auto [best_version, effective_arch] = _find_best_version(pkg_name, target_arch, constraints);
        
        if (!best_version.empty() && !effective_arch.empty()) {
            resolved_list.push_back(Depend{pkg_name, best_version, effective_arch});
            
        } else {
            Depend res = this->handle_virtual_packages(pkg_name, target_arch);
            if(res.name == "")
                throw std::runtime_error("Could not resolve version for package: " + pkg_name + " with given constraints");
            
            resolved_list.push_back(res);
            
        }
    }

    return resolved_list;
}

Depend DebianPackageService::handle_virtual_packages(const std::string& name, const std::string& parent_arch) {
    for (char c : name) {
        if (!std::isalnum(c) && c != '-' && c != '.' && c != '+' && c != ':') { 
            spdlog::error("Invalid character in virtual package name: '{}'", name); 
            return Depend{"", "", ""};
        }
    }
    
    std::string search_name = name;
    size_t name_colon = search_name.find(':');
    if (name_colon != std::string::npos) { 
        search_name = search_name.substr(0, name_colon); 
    }

    std::string output = execute_command("apt-cache showpkg " + search_name).stdout_res;

    std::istringstream iss(output);
    std::string line;
    bool in_reverse_provides = false;
    
    std::string real_name = "";
    std::string version = "";
    std::string arch = parent_arch;

    while (std::getline(iss, line)) { 
        if (line.find(std::string(DebianFields::R_PROVIDES) + ":") == 0) { 
            in_reverse_provides = true; 
            continue;
        }

        if (!in_reverse_provides) { 
            continue;
        }

        if (line.empty() || line.find('(') == std::string::npos) { 
            break; 
        }

        std::istringstream line_stream(line);
        std::string full_name;
        std::string temp_ver;
        
        if (!(line_stream >> full_name >> temp_ver)) { 
            continue; 
        }
        
        size_t colon_pos = full_name.find(':');
        
        if (colon_pos == std::string::npos) { 
            real_name = full_name; 
            version = temp_ver;
            break;
        }

        std::string parsed_name = full_name.substr(0, colon_pos);
        std::string pkg_arch = full_name.substr(colon_pos + 1);

        if (pkg_arch != parent_arch && pkg_arch != "all" && pkg_arch != "any") { 
            continue; 
        }
        
        real_name = parsed_name;
        version = temp_ver;
        break;
    }

    if (real_name.empty()) {
        throw std::runtime_error("Failed to resolve virtual package: " + name);
        return Depend{"", "", ""};
    }

    spdlog::info("Virtual package {} resolved to {} with version {} and arch {}", name, real_name, version, arch);
    return Depend{real_name, version, arch};
}

std::vector<json> DebianPackageService::get_package_instances(const std::string& pkg_name) noexcept{
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

    if (file_info_map.contains(target_hash) && !file_info_map[target_hash].empty()) {
        json file_info = file_info_map[target_hash][0];
        file_info[DebianFields::SHA1] = target_hash; 
        return file_info;
    }

    return nullptr;
}


PackageMetadata DebianPackageService::get_package_metadata(const std::string& file_path) {
    auto control_data = _get_raw_control_data(file_path);

    std::string sha256 = _calculate_sha256(file_path);
    std::string arch = control_data[DebianFields::ARCH];
    
    std::string combined_depends = control_data[DebianFields::DEPENDS];
    if (!control_data[DebianFields::PRE_DEPENDS].empty()) {
        if (!combined_depends.empty()) combined_depends += ", ";
        combined_depends += control_data[DebianFields::PRE_DEPENDS];
    }

    auto resolved_deps = _resolve_dependencies(combined_depends, arch);

    PackageMetadata metadata;
    metadata.Package = control_data[DebianFields::PACKAGE];
    metadata.Version = control_data[DebianFields::VERSION];
    metadata.Type = DebianFields::Debian; 
    metadata.Architecture = arch;
    metadata.Dependencies = resolved_deps;
    metadata.SHA256 = sha256;
    metadata.compute_store_path(); 
    metadata.Installed_Size = control_data.count(DebianFields::INSTALLED_SIZE) ? std::stoll(control_data[DebianFields::INSTALLED_SIZE]) : 0;
    
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
        
        if (arch == target_arch) {
            return target_arch;
        }
        
        if (arch == "all") {
            has_all = true;
        }
    }

    return has_all ? "all" : "";
}

bool DebianPackageService::_compare_versions(const std::string& v1, 
                                            const std::string& op, 
                                            const std::string& v2) noexcept{

    std::string dpkg_op = DpkgOps::op_map.count(op) ? DpkgOps::op_map.at(op) : DpkgOps::CMD_EQ;
    std::string cmd = "dpkg --compare-versions '" + v1 + "' " + dpkg_op + " '" + v2 + "'";

    int exit_code = execute_status_cmd(cmd);
    return exit_code == 0;
}

std::map<std::string, std::string> DebianPackageService::_get_raw_control_data(const std::string& file_path) {
    if (!std::filesystem::exists(file_path)) {
        throw std::runtime_error("File not found: " + file_path);
    }

    CommandResult res = execute_command("dpkg-deb -I '" + file_path + "'");

    if (res.exit_code != 0) {
        throw std::runtime_error("dpkg-deb error: " + res.stderr_res);
    }

    std::map<std::string, std::string> control_data;
    std::stringstream ss(res.stdout_res);
    std::string line;

    while (std::getline(ss, line)) {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);

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

std::pair<std::string, std::string> DebianPackageService::_find_best_version(
    const std::string& pkg_name,
    const std::string& target_arch,
    const std::vector<std::pair<std::string, std::string>>& constraints) noexcept
{
    std::vector<json> instances = get_package_instances(pkg_name);
    std::vector<std::string> all_versions;
    all_versions.reserve(instances.size());
    
    for (const auto& i : instances) {
        if (i.contains("binary_version") && i["binary_version"].is_string()) {
            std::string ver = i["binary_version"].get<std::string>();

            all_versions.push_back(ver);
        }
    }

    if (all_versions.empty()) 
        return std::make_pair("", "");
    
    std::pair<std::string, std::string> best_version_arch;
    for (const std::string& v : all_versions) {
        bool is_valid = true;
        
        for (const auto& constraint : constraints) {
            const std::string& op = constraint.first;
            const std::string& target_ver = constraint.second;

            if (!_compare_versions(v, op, target_ver)) {
                is_valid = false;
                break; 
            }

        }

        if (is_valid) {
            std::string v_arch = _target_arch_or_all(pkg_name, v, target_arch);
            if(v_arch.empty())
                continue; 
            
            best_version_arch = std::make_pair(v, v_arch);
            break;
        }
    }
    if (best_version_arch.first.empty() || best_version_arch.second.empty()) 
        throw std::runtime_error("No valid version or arch found for " + pkg_name + " that satisfies all constraints");
    
    return best_version_arch;
}

std::map<std::string, std::vector<std::pair<std::string, std::string>>> 
DebianPackageService::_parse_constraints(const std::string& depends_str) 
{
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> package_constraints;
    
    if (depends_str.empty()) {
        return package_constraints;
    }

    std::stringstream ss(depends_str);
    std::string part;

    while (std::getline(ss, part, ',')) {
        size_t pipe_pos = part.find('|');
        std::string preferred_dep = (pipe_pos != std::string::npos) ? part.substr(0, pipe_pos) : part;

        size_t start = preferred_dep.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue; 

        size_t end = preferred_dep.find_last_not_of(" \t\r\n");
        preferred_dep = preferred_dep.substr(start, end - start + 1);

        std::smatch match;
        if (!std::regex_search(preferred_dep, match, DpkgOps::op_regex)) {
            continue;
        }

        std::string pkg_name = match[1].str();
        package_constraints[pkg_name];

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

    char buffer[64 * 1024]; 
    while (file.read(buffer, sizeof(buffer))) {
        SHA1_Update(&sha1_ctx, buffer, file.gcount());
    }
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
    json pkg_meta = get_package_info(pkg_name, version, architecture);
    if (pkg_meta.empty() || pkg_meta.is_null()) {
        throw std::runtime_error("No binary found for " + pkg_name + " " + version + " (" + architecture + ")");
    }

    std::string expected_sha1 = pkg_meta.value(DebianFields::SHA1, "");
    fs::path file_name = this->download_path / pkg_meta.value("name", "");
    std::string file_name_str = file_name.string();
    std::string first_seen = pkg_meta.value("first_seen", "");
    std::string path_str = pkg_meta.value("path", "");

    if (file_name_str.empty()) {
        throw std::runtime_error("Missing file name in metadata for " + pkg_name);
    }

    if(fs::exists(file_name_str)) {
        std::string actual_sha1 = _calculate_sha1(file_name_str);
        if (actual_sha1 == expected_sha1) {
            spdlog::info("File already exists and is valid: {}", file_name_str);
            return file_name_str;
        } else {
            spdlog::warn("Existing file {} has invalid SHA1. Expected: {}, Actual: {}. Redownloading...", 
                         file_name_str, expected_sha1, actual_sha1);
            fs::remove(file_name_str);
        }
    }

    std::string endpoint = "/archive/debian/" + first_seen + "/" + path_str + "/" + encode_url(file_name.filename().string());

    httplib::Result res;
    bool success = false;

    for (int try_count = 0; try_count <= MAX_DOWNLOAD_RETRIES; ++try_count) {
        std::ofstream outfile(file_name_str, std::ios::binary | std::ios::trunc);
        
        if (!outfile.is_open()) {
            throw std::runtime_error("Failed to open local file: " + file_name_str);
        }

        res = client->Get(endpoint.c_str(), [&](const char *data, size_t data_length) {
            outfile.write(data, data_length);
            return outfile.good();
        });

        outfile.close();

        if (res && res->status == 200) {
            success = true;
            break; 
        }

        bool is_network_error = !res;
        bool is_server_error = res && res->status >= 500;

        if (is_network_error || is_server_error) {
            spdlog::warn("Attempt {} failed for {}. Retrying in 2s...", try_count + 1, file_name_str);
            std::this_thread::sleep_for(std::chrono::seconds(2));
        } else break; 
        
    }

    if (!success) {
        std::remove(file_name_str.c_str());
        std::string err_msg = "Final download failure for " + file_name_str + ": ";
        err_msg += res ? std::to_string(res->status) : httplib::to_string(res.error());
        throw std::runtime_error(err_msg);
    }

    std::string actual_sha1 = _calculate_sha1(file_name_str);
    
    if (!expected_sha1.empty() && actual_sha1 != expected_sha1) {
        std::remove(file_name_str.c_str());
        throw std::runtime_error(
            "Integrity check failed for " + file_name_str + "!\n"
            "Expected: " + expected_sha1 + "\n"
            "Actual:   " + actual_sha1
        );
    }

    spdlog::info("Downloaded and verified: {}", file_name_str);
    return file_name_str;
}

std::vector<PackageMetadata> DebianPackageService::get_recursive_dependencies(
    const PackageMetadata& metadata, 
    const std::string& file_path,
    const constraint_map& injected_constraints) 
{
    ResolutionContext ctx;
    ctx.arch = metadata.Architecture;
    ctx.enqueued.insert(metadata.Package);
    ctx.resolved_packages[metadata.Package] = metadata; 
    ctx.global_constraints = injected_constraints;

    std::map<std::string, std::string> root_control = _get_raw_control_data(file_path);
    parse_and_queue_control_data(ctx, root_control, metadata);

    try {
        this->resolve_queued_packages(ctx);

        this->remove_ghost_dependencies(ctx);

        this->link_dependency(ctx);
        
        return this->extract_final_metadata(ctx, metadata.Package);
        
    } catch (const std::exception& e) {
        this->cleanup_download_path();
        
        throw std::runtime_error("Dependency resolution failed: " + std::string(e.what()));
    }
}

void DebianPackageService::process_dependency_requirements(
    ResolutionContext& ctx, 
    const std::string& pkg, 
    const std::vector<std::pair<std::string, std::string>>& reqs, 
    const PackageMetadata& parent_metadata, 
    bool invert, 
    bool is_essential_break) 
{
    if (DebianFields::IGNORE_PACKAGES.count(pkg)) return;

    auto& current_constraints_for_pkg = ctx.global_constraints[pkg];
    std::vector<std::pair<std::string, std::string>> new_constraints;
    
    for (const auto& req : reqs) {
        std::string op = req.first;
        std::string ver = req.second;
        
        if (invert) {
            auto it = DpkgOps::inverse_map.find(op);
            if (it != DpkgOps::inverse_map.end()) {
                op = it->second;
            } else {
                spdlog::warn("Unknown operator {} for inversion. Defaulting to '='", op);
                op = DpkgOps::EQ;
            }
        }
        
        new_constraints.push_back({op, ver});
        current_constraints_for_pkg.push_back({op, ver});
    }
    
    if (ctx.resolved_packages.count(pkg)) {
        std::string resolved_ver = ctx.resolved_packages[pkg].Version;
        for (const auto& req : new_constraints) {
            if (!_compare_versions(resolved_ver, req.first, req.second)) {
                spdlog::warn("Version conflict! Have {} v{}, but need {} {}. Re-queueing to find middle ground.", 
                             pkg, resolved_ver, req.first, req.second);
                ctx.queue.push(pkg);
                break;
            }
        }
    }
    
    bool should_queue = !invert || is_essential_break;
    
    if (should_queue) {
        ctx.dependency_links.push_back({parent_metadata.Package, pkg});
        
        if (!ctx.enqueued.count(pkg)) {
            ctx.enqueued.insert(pkg);
            ctx.queue.push(pkg);
        }
    }
}

void DebianPackageService::parse_and_queue_control_data(
    ResolutionContext& ctx, 
    const std::map<std::string, std::string>& c_data, 
    const PackageMetadata& parent_metadata) 
{
    std::string d_str = c_data.count(DebianFields::DEPENDS) ? c_data.at(DebianFields::DEPENDS) : "";
    std::string pd_str = c_data.count(DebianFields::PRE_DEPENDS) ? c_data.at(DebianFields::PRE_DEPENDS) : "";
    
    std::string combined_depends = d_str;
    if (!pd_str.empty()) {
        if (!combined_depends.empty()) combined_depends += ", ";
        combined_depends += pd_str;
    }

    auto parsed_d = _parse_constraints(combined_depends);
    for (const auto& [dep_pkg, reqs] : parsed_d) {
        process_dependency_requirements(ctx, dep_pkg, reqs, parent_metadata, false, false);
    }

    if (c_data.count(DebianFields::REPLACES)) {
        std::string rep_str = c_data.at(DebianFields::REPLACES);
        std::stringstream ss(rep_str);
        std::string part;
        while (std::getline(ss, part, ',')) {
            size_t pipe_pos = part.find('|');
            std::string cleaned = (pipe_pos != std::string::npos) ? part.substr(0, pipe_pos) : part;
            
            size_t start = cleaned.find_first_not_of(" \t\r\n");
            if (start != std::string::npos) {
                cleaned = cleaned.substr(start);
                size_t space_pos = cleaned.find(' ');
                std::string r_pkg = (space_pos != std::string::npos) ? cleaned.substr(0, space_pos) : cleaned;
                ctx.replaces_provides.insert(r_pkg);
            }
        }
    }

    if (c_data.count(DebianFields::BREAK)) {
        auto parsed_brk = _parse_constraints(c_data.at(DebianFields::BREAK));
        for (const auto& [brk_pkg, reqs] : parsed_brk) {
            bool is_essential = DebianFields::ESSENTIAL_PACKAGES.count(brk_pkg) > 0;
            process_dependency_requirements(ctx, brk_pkg, reqs, parent_metadata, true, is_essential);
        }
    }
}

void DebianPackageService::resolve_queued_packages(ResolutionContext& ctx) 
{
    while (!ctx.queue.empty()) {
        std::string current_pkg = ctx.queue.front();
        ctx.queue.pop();

        std::vector<std::pair<std::string, std::string>> constraints = ctx.global_constraints[current_pkg];
        auto [best_version, target_arch] = _find_best_version(current_pkg, ctx.arch, constraints);

        if (best_version.empty()) {
            try {
                Depend v_dep = this->handle_virtual_packages(current_pkg, ctx.arch);
                
                if (!v_dep.name.empty() && v_dep.name != current_pkg) {
                    spdlog::info("Package '{}' is virtual. Re-routing queue to real package '{}'", current_pkg, v_dep.name);
                    
                    auto& real_constraints = ctx.global_constraints[v_dep.name];
                    std::tie(best_version, target_arch) = _find_best_version(v_dep.name, ctx.arch, real_constraints);

                    if (best_version.empty()) 
                        throw std::runtime_error("Virtual package " + current_pkg + " resolved to " + v_dep.name + ", but no version satisfies constraints.");
                    
                    for(const auto& c : constraints) {
                        real_constraints.push_back(c);
                    }
                    
                    for (auto& link : ctx.dependency_links) {
                        if (link.second == current_pkg) {
                            link.second = v_dep.name;
                        }
                    }
                    
                    if (!ctx.enqueued.count(v_dep.name)) {
                        ctx.enqueued.insert(v_dep.name);
                        ctx.queue.push(v_dep.name);
                    }
                    
                    continue;
                }
            } catch (const std::exception& e) {
                spdlog::debug("Virtual package resolution failed for {}: {}", current_pkg, e.what());
            }

            throw std::runtime_error("Unresolvable constraints! No version of " + current_pkg + " satisfies the requirements.");
        }

        if (ctx.resolved_packages.count(current_pkg) && ctx.resolved_packages[current_pkg].Version == best_version) {
            continue;
        }

        std::string dl_path;
        try {
            dl_path = get_package_file(current_pkg, best_version, target_arch);
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to download " + current_pkg + ": " + std::string(e.what()));
        }

        PackageMetadata pkg_meta = get_package_metadata(dl_path);
        ctx.resolved_packages[current_pkg] = pkg_meta;

        std::map<std::string, std::string> curr_control = _get_raw_control_data(dl_path);
        parse_and_queue_control_data(ctx, curr_control, pkg_meta);
    }
}

void DebianPackageService::remove_ghost_dependencies(ResolutionContext& ctx) 
{
    for (auto& [pkg_name, meta] : ctx.resolved_packages) {
        auto it = meta.Dependencies.begin();
        while (it != meta.Dependencies.end()) {
            const std::string& dep_name = (*it).name;
            
            if (ctx.resolved_packages.find(dep_name) == ctx.resolved_packages.end()) {
                spdlog::warn("Removing ghost dependency {} from {}", dep_name, pkg_name);
                it = meta.Dependencies.erase(it);
            } else {
                (*it).version = ctx.resolved_packages[dep_name].Version;
                (*it).arch = ctx.resolved_packages[dep_name].Architecture;
                ++it;
            }
        }
    }
}

void DebianPackageService::link_dependency(ResolutionContext& ctx) 
{
    for (const auto& link : ctx.dependency_links) {
        const std::string& parent_name = link.first;
        const std::string& child_name = link.second;

        if (ctx.resolved_packages.count(parent_name) && ctx.resolved_packages.count(child_name)) {
            PackageMetadata& parent_meta = ctx.resolved_packages[parent_name];
            const PackageMetadata& child_meta = ctx.resolved_packages[child_name];
            
            bool exists = false;
            for (const auto& dep : parent_meta.Dependencies) {
                if (dep.name == child_name) { exists = true; break; }
            }
            
            if (!exists) {
                parent_meta.Dependencies.push_back({
                    child_meta.Package, 
                    child_meta.Version, 
                    child_meta.Architecture
                });
            }
        }
    }
}

std::vector<PackageMetadata> DebianPackageService::extract_final_metadata(
    const ResolutionContext& ctx, 
    const std::string& root_package) 
{
    std::vector<PackageMetadata> final_metadata_list;
    final_metadata_list.reserve(ctx.resolved_packages.size() - 1);
    
    for (const auto& [pkg_name, meta] : ctx.resolved_packages) {
        final_metadata_list.push_back(meta);
    }
    return final_metadata_list;
}

void DebianPackageService::cleanup_download_path() 
{
    try {
        if (fs::exists(this->download_path)) {
            fs::remove_all(this->download_path);
        }
        fs::create_directories(this->download_path);
    } catch (const std::exception& inner_e) { 
        spdlog::critical("Cleanup failed: {}", inner_e.what());
    }
}

bool DebianPackageService::is_system_pkg(const std::string &pkg_name) const
{
    if (pkg_name == "libc6") return true;
    return false;
}

provider_map DebianPackageService::build_provider_map(const std::vector<PackageMetadata>& all_pkgs) {
    provider_map p_map;

    for (const auto& pkg : all_pkgs) {
        if (!fs::exists(this->download_path / pkg.Filename)) continue;

        if(this->is_system_pkg(pkg.Package)) {
            spdlog::info("Skipping provider map extraction for system package: {}", pkg.Package);
            continue;
        }
        fs::path map_key = pkg.Store_Path;
        
        std::string cmd = "dpkg-deb --fsys-tarfile " + (this->download_path / pkg.Filename).string();
        FILE *pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            spdlog::error("Failed to run dpkg-deb stream on {}", pkg.Package);
            continue;
        }
        
        struct archive *a = archive_read_new();
        archive_read_support_filter_all(a);
        archive_read_support_format_all(a);
        
        if (archive_read_open_FILE(a, pipe) != ARCHIVE_OK) {
            spdlog::error("Failed to open archive stream for {}", pkg.Package);
            archive_read_free(a);
            pclose(pipe);
            continue;
        }
        
        struct archive_entry *entry;
        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            
            std::string path_str = archive_entry_pathname(entry);
            if (path_str.substr(0, 2) == "./") path_str.erase(0, 2);

            if (!has_allowed_prefix(path_str)) continue;

            auto type = archive_entry_filetype(entry);
            if (type != S_IFREG && type != S_IFLNK) continue;

            fs::path path(path_str);
            mode_t mode = archive_entry_mode(entry);

            bool is_so = (path.extension() == ".so" || path_str.find(".so.") != std::string::npos);
            bool is_exe = (type == S_IFREG) && (mode & 0111) && is_in_bin_dir(path_str);

            // Handle SONAME 
            if (type == S_IFREG && is_so) {
                if (auto soname = this->extract_soname_from_archive(a)) {
                    std::string soname_path = path.parent_path().string() + "/" + *soname;
                    if (soname_path != path_str) {
                        p_map[map_key].emplace_back(soname_path, path_str, false);
                    }
                }
            }

            p_map[map_key].emplace_back(path_str, path_str, is_exe);
        }
        
        archive_read_free(a);
        pclose(pipe);
    }
    
    return p_map;
}

void DebianPackageService::_collect_subgraph_deps(
    std::unordered_set<std::string>& bundled_deps, 
    const std::string& dep_name, 
    const std::string& version, 
    const GSO& global_sort) const
{
    auto sys_deps = global_sort.subgraph_order(dep_name, version);
    for (const auto& sys_dep : sys_deps) {
        std::string sys_dep_name = sys_dep.Package;
        if (sys_dep_name != dep_name) {
            bundled_deps.insert(sys_dep_name);
        }
    }
}

std::unordered_set<std::string> DebianPackageService::_get_bundled_dependencies(
    const std::vector<PackageMetadata>& full_deps, 
    const std::string& current_pkg_name, 
    const GSO& global_sort) const
{
    std::unordered_set<std::string> bundled_deps;
    
    for (const auto& dep : full_deps) {
        std::string dep_name = dep.Package;
        if (this->is_system_pkg(dep_name) && dep_name != current_pkg_name) {
            _collect_subgraph_deps(bundled_deps, dep_name, dep.Version, global_sort);
        }
    }
    
    return bundled_deps;
}

void DebianPackageService::_insert_provided_dependencies(
    const fs::path& dec_path, 
    const provider_vector& providers, 
    std::map<std::string, fs::path>& result_submap) const
{
    for (const auto& [provided_name, provided_soname, is_executable] : providers) {
        result_submap.insert({
            provided_soname, 
            dec_path / provided_name
        });
    }
}

forest_map DebianPackageService::generate_forests(const provider_map &global_provider_map, const GSO &global_sort)
{
    forest_map results;
    const std::vector<PackageMetadata>& sorted_pkgs = global_sort.get_sorted_pkgs();
    
    for (const auto& pkg : sorted_pkgs) {
        const auto name = pkg.Package;
        const auto version = pkg.Version;
        const auto store_path = pkg.Store_Path;

        auto full_deps = global_sort.subgraph_order(name, version);
        
        std::unordered_set<std::string> bundled_deps = _get_bundled_dependencies(full_deps, name, global_sort);

        for (const PackageMetadata& dec_pkg : full_deps) {
            std::string dec_name = dec_pkg.Package;
            
            if (bundled_deps.find(dec_name) != bundled_deps.end()) {
                continue;
            }

            fs::path dec_path = dec_pkg.Store_Path;
            auto provider_it = global_provider_map.find(dec_path);

            if (dec_path == store_path || provider_it == global_provider_map.end()) {
                continue; 
            }
                
            spdlog::info("For package {}-{}, checking dependencies in subgraph order: {}", name, version, dec_name);

            _insert_provided_dependencies(dec_path, provider_it->second, results[store_path]);
        }
    }

    return results;
}

json DebianPackageService::get_status(const std::string &filename) {
    fs::path path = this->download_path / filename;
    
    if (!fs::exists(path)) {
        spdlog::error("File not found for status check: {}", path.string());
        return nullptr;
    }

    try {
        std::string control_raw = execute_command("dpkg-deb -f " + path.string()).stdout_res;
        
        std::string name = "unknown";
        std::string arch = "unknown";
        std::stringstream ss(control_raw);
        std::string line;
        std::string status_block;

        while (std::getline(ss, line)) {
            status_block += line + "\n";
            if (line.find(std::string(DebianFields::PACKAGE) + ": ") == 0) {
                name = line.substr(9);
                status_block += "Status: install ok installed\n";
            } else if (line.find(std::string(DebianFields::ARCH) + ": ") == 0) {
                arch = line.substr(14);
            }
        }

        std::string file_list_raw = execute_command("dpkg-deb -c " + path.string()).stdout_res;
        std::vector<std::string> files;
        std::stringstream fs_ss(file_list_raw);
        
        while (std::getline(fs_ss, line)) {
            size_t last_space = line.find_last_of(' ');
            if (last_space != std::string::npos) {
                std::string file_path = line.substr(last_space + 1);
                if (file_path.front() == '.') {
                    file_path.erase(0, 1);
                }
                if (!file_path.empty() && file_path != "/") {
                    files.push_back(file_path);
                }
            }
        }

        json record;
        record["name"] = name;
        record["arch"] = arch;
        record["status_block"] = status_block;
        record["files"] = files;
        
        return record;

    } catch (const std::exception& e) {
        spdlog::error("Failed to process .deb metadata: {}", e.what());
        return nullptr;
    }
}
