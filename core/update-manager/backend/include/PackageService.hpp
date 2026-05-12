#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <sstream>
#include <regex>
#include <fstream>
#include <iostream>
#include <memory>
#include <spawn.h>
#include <sys/wait.h>
#include <algorithm>
#include <iomanip>
#include <cstdio>
#include <stdexcept>

// Third-party headers
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <archive.h>
#include <archive_entry.h>
#include <openssl/sha.h>
#include <spdlog/spdlog.h>

#include "Graphs.hpp"
#include "Algorithms.hpp"
#include "utils.hpp"

/**
 * Abstract Base Class defining the required interface for all package service 
 * implementations (e.g., Debian, PyPI, RPM).
 */
class PackageService {
public:
    PackageService(fs::path download_dir) : download_path(std::move(download_dir)) {}
    
    virtual ~PackageService() = default;

    fs::path download_path;

    /**
     * Initializes the package service, typically by creating necessary resources 
     * (e.g., configuring an HTTP client).
     */
    virtual void init() = 0;

    /**
     * Cleans up the package service resources, typically closing connections.
     */
    virtual void cleanup() = 0;

    /**
     * Retrieves a list of all available versions and instances for a given package name 
     * from the service's repository.
     * 
     * @param pkg_name The name of the package.
     * @return A vector of JSON objects, where each represents a package instance.
     */
    virtual std::vector<json> get_package_instances(const std::string& pkg_name) = 0;

    /**
     * Fetches detailed metadata about a specific binary package instance 
     * (version/architecture combination) from the service's repository.
     * 
     * @param pkg_name The name of the package.
     * @param version The version of the package.
     * @param architecture The target hardware architecture (e.g., 'amd64').
     * @return A JSON object containing the package metadata.
     */
    virtual json get_package_info(const std::string& pkg_name, const std::string& version, const std::string& architecture) = 0;

    /**
     * Locates the package file, downloads it to a local path, and verifies its 
     * integrity (e.g., hash check) using information from the service.
     * 
     * @param pkg_name The name of the package.
     * @param version The version of the package.
     * @param architecture The target hardware architecture.
     * @return The local file path to the downloaded package file.
     */
    virtual std::string get_package_file(const std::string& pkg_name, const std::string& version, const std::string& architecture) = 0;

    /**
     * Parses the package control information from a locally downloaded file
     * and returns a standardized PackageMetadata object. This typically involves
     * using a system tool (like dpkg-deb for Debian) and resolving dependencies.
     * 
     * @param file_path The local path to the package file.
     * @return A PackageMetadata object containing parsed information.
     */
    virtual PackageMetadata get_package_metadata(const std::string& file_path) = 0;

    /**
     * Dynamically resolves the full dependency tree for a given package. This method 
     * should handle complex scenarios such as multi-constraint intersections, 
     * re-evaluating packages if new constraints are discovered, and ensuring essential 
     * packages are included if they appear in 'Breaks'.
     * 
     * @param metadata The PackageMetadata of the root package.
     * @param file_path The local path to the root package file.
     * @return A vector of PackageMetadata objects representing all resolved dependencies.
     */
    virtual std::vector<PackageMetadata> get_recursive_dependencies(const PackageMetadata& metadata, const std::string& file_path, const constraint_map& injected_constraints) = 0;

    virtual bool is_system_pkg(const std::string& pkg_name) = 0;

    virtual provider_map build_provider_map(const std::vector<PackageMetadata>& all_pkgs) = 0;

    virtual forest_map generate_forests(const provider_map& global_provider_map, const GSO& global_sort) = 0;

    virtual json get_status(const std::string& path) = 0;

};

class DebianPackageService : public PackageService {
public:
    DebianPackageService(fs::path download_dir) : PackageService(download_dir) {}
    
    ~DebianPackageService() override = default;

    // Interface Implementation
    void init() override;
    void cleanup() override;
    
    std::vector<json> get_package_instances(const std::string& pkg_name) override;
    
    json get_package_info(const std::string& pkg_name, 
                                   const std::string& version, 
                                   const std::string& architecture) override;
                                   
    std::string get_package_file(const std::string& pkg_name, 
                                const std::string& version, 
                                const std::string& architecture) override;
                                
    PackageMetadata get_package_metadata(const std::string& file_path) override;
    
    std::vector<PackageMetadata> get_recursive_dependencies(const PackageMetadata& metadata, 
                                                            const std::string& file_path,
                                                            const constraint_map& injected_constraints) override;

    bool is_system_pkg(const std::string& pkg_name) override;

    provider_map build_provider_map(const std::vector<PackageMetadata>& all_pkgs) override;

    forest_map generate_forests(const provider_map& global_provider_map, const GSO& global_sort) override ;

    json get_status(const std::string& path) override;

private:
    // Constants
    const std::string BASE_URL = "snapshot.debian.org"; // httplib handles the protocol separately
    const size_t BUFFER_SIZE = 64 * 1024;

    // Internal HTTP client (using std::unique_ptr for lifecycle management)
    std::unique_ptr<httplib::Client> client;

    // Private Helpers
    json _get_json(const std::string& endpoint);
    
    bool _compare_versions(const std::string& v1, const std::string& op, const std::string& v2);
    
    std::string _find_best_version(const std::string& pkg_name, 
                                   const std::vector<std::pair<std::string, std::string>>& constraints);
    
    std::string _target_arch_or_all(const std::string& pkg_name, 
                                    const std::string& version, 
                                    const std::string& target_arch);
    
    std::map<std::string, std::string> _get_raw_control_data(const std::string& file_path);
    
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> _parse_constraints(const std::string& depends_str);
    
    std::vector<std::vector<std::string>> _resolve_dependencies(const std::string& depends_str, 
                                                                const std::string& target_arch);

    // Hashing helper for integrity
    std::string _calculate_sha1(const std::string& file_path);
    std::string _calculate_sha256(const std::string& file_path);

    std::vector<std::string> get_elf_tags(const std::string& path, const std::string& tag);

    std::optional<std::string> extract_soname_from_archive(struct archive* a);

};