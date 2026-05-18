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
 * @brief Abstract Base Class defining the package service interface.
 */
class PackageService {
public:
    PackageService(fs::path download_dir) : download_path(std::move(download_dir)) {}
    
    virtual ~PackageService() = default;

    fs::path download_path; ///< Directory path where package files are downloaded.

    /**
     * @brief Initializes package service resources.
     */
    virtual void init() = 0;

    /**
     * @brief Cleans up service resources and connections.
     */
    virtual void cleanup() = 0;

    /**
     * @brief Retrieves available versions and instances for a package.
     * @param pkg_name The name of the package.
     * @return std::vector<json> List of JSON objects representing package instances.
     */
    virtual std::vector<json> get_package_instances(const std::string& pkg_name) = 0;

    /**
     * @brief Fetches detailed metadata for a specific package instance.
     * @param pkg_name The name of the package.
     * @param version The version of the package.
     * @param architecture The target hardware architecture.
     * @return json Package metadata object.
     */
    virtual json get_package_info(const std::string& pkg_name, const std::string& version, const std::string& architecture) = 0;

    /**
     * @brief Downloads a package file and verifies its integrity.
     * @param pkg_name The name of the package.
     * @param version The version of the package.
     * @param architecture The target hardware architecture.
     * @return std::string Local file path to the downloaded package.
     */
    virtual std::string get_package_file(const std::string& pkg_name, const std::string& version, const std::string& architecture) = 0;

    /**
     * @brief Parses local file control info to generate package metadata.
     * @param file_path Local path to the package file.
     * @return PackageMetadata Parsed package information.
     */
    virtual PackageMetadata get_package_metadata(const std::string& file_path) = 0;

    /**
     * @brief Resolves the full dependency tree for a package.
     * @param metadata The PackageMetadata of the root package.
     * @param file_path Local path to the root package file.
     * @param injected_constraints Pre-existing constraints to apply during resolution.
     * @return std::vector<PackageMetadata> List of resolved dependencies.
     */
    virtual std::vector<PackageMetadata> get_recursive_dependencies(const PackageMetadata& metadata, const std::string& file_path, const constraint_map& injected_constraints) = 0;

    /**
     * @brief Checks if a package is a core system requirement.
     * @param pkg_name The name of the package.
     * @return true if it is a system package, false otherwise.
     */
    virtual bool is_system_pkg(const std::string& pkg_name) = 0;

    /**
     * @brief Builds a map of files and libraries provided by downloaded packages.
     * @param all_pkgs List of downloaded package metadata.
     * @return provider_map Map linking store paths to their provided files.
     */
    virtual provider_map build_provider_map(const std::vector<PackageMetadata>& all_pkgs) = 0;

    /**
     * @brief Maps dependencies to their required providers using subgraph ordering.
     * @param global_provider_map Map of all available providers.
     * @param global_sort Graph sort object containing the dependency order.
     * @return forest_map Map of symbolic link forests required by each package.
     */
    virtual forest_map generate_forests(const provider_map& global_provider_map, const GSO& global_sort) = 0;

    /**
     * @brief Extracts installation status and file lists from a package.
     * @param path Local path to the package file.
     * @return json Object containing status block and file tree.
     */
    virtual json get_status(const std::string& path) = 0;
};

/**
 * @brief Debian specific implementation of the PackageService.
 */
class DebianPackageService : public PackageService {
public:
    DebianPackageService(fs::path download_dir) : PackageService(download_dir) {}
    
    ~DebianPackageService() override = default;

    /**
     * @brief Configures the HTTP client for the Debian snapshot API.
     */
    void init() override;

    /**
     * @brief Resets the internal HTTP client.
     */
    void cleanup() override;
    
    /**
     * @brief Fetches package instance data from the Debian API.
     * @param pkg_name The name of the package.
     * @return std::vector<json> List of instances.
     */
    std::vector<json> get_package_instances(const std::string& pkg_name) override;
    
    /**
     * @brief Retrieves binary file info and hashes from the Debian API.
     * @param pkg_name The name of the package.
     * @param version The version of the package.
     * @param architecture The target hardware architecture.
     * @return json Metadata containing the file hash.
     */
    json get_package_info(const std::string& pkg_name, 
                                   const std::string& version, 
                                   const std::string& architecture) override;
                                   
    /**
     * @brief Downloads a .deb file and verifies its SHA1 hash.
     * @param pkg_name The name of the package.
     * @param version The version of the package.
     * @param architecture The target hardware architecture.
     * @return std::string Path to the downloaded .deb file.
     */
    std::string get_package_file(const std::string& pkg_name, 
                                const std::string& version, 
                                const std::string& architecture) override;
                                
    /**
     * @brief Extracts metadata and initial dependencies from a .deb file using dpkg-deb.
     * @param file_path Path to the .deb file.
     * @return PackageMetadata Extracted metadata.
     */
    PackageMetadata get_package_metadata(const std::string& file_path) override;
    
    /**
     * @brief Resolves the Debian dependency graph using constraints and control data.
     * @param metadata Root package metadata.
     * @param file_path Root .deb file path.
     * @param injected_constraints Applied constraints.
     * @return std::vector<PackageMetadata> Resolved dependency tree.
     */
    std::vector<PackageMetadata> get_recursive_dependencies(const PackageMetadata& metadata, 
                                                            const std::string& file_path,
                                                            const constraint_map& injected_constraints) override;

    /**
     * @brief Checks if a Debian package is a core system package (e.g., libc6).
     * @param pkg_name The name of the package.
     * @return true if system package, false otherwise.
     */
    bool is_system_pkg(const std::string& pkg_name) override;

    /**
     * @brief Extracts files, executables, and SONAMEs from .deb archives using libarchive.
     * @param all_pkgs List of packages to process.
     * @return provider_map Map of provided paths and SONAMEs.
     */
    provider_map build_provider_map(const std::vector<PackageMetadata>& all_pkgs) override;

    /**
     * @brief Generates symbolic link requirements for Debian packages.
     * @param global_provider_map Providers mapped from .deb files.
     * @param global_sort Graph containing dependency order.
     * @return forest_map Symbolic link definitions.
     */
    forest_map generate_forests(const provider_map& global_provider_map, const GSO& global_sort) override ;

    /**
     * @brief Extracts dpkg status block and file tree from a .deb using dpkg-deb.
     * @param path Path to the .deb file.
     * @return json Status block and file list.
     */
    json get_status(const std::string& path) override;

private:
    const std::string BASE_URL = "snapshot.debian.org"; ///< Base URL for Debian snapshot API.
    const size_t BUFFER_SIZE = 64 * 1024; ///< Buffer size used for file operations and hashing.

    std::unique_ptr<httplib::Client> client; ///< Internal HTTP client instance.

    /**
     * @brief Performs an HTTP GET request and parses the JSON response.
     * @param endpoint The API path to request.
     * @return json Parsed JSON object or nullptr on failure.
     */
    json _get_json(const std::string& endpoint);
    
    /**
     * @brief Compares two Debian package versions using dpkg.
     * @param v1 First version.
     * @param op Operator (e.g., "<<", ">=").
     * @param v2 Second version.
     * @return true if the comparison holds, false otherwise.
     */
    bool _compare_versions(const std::string& v1, const std::string& op, const std::string& v2);
    
    /**
     * @brief Finds the highest available version satisfying given constraints.
     * @param pkg_name The package name.
     * @param constraints List of version constraints.
     * @return std::string Best matching version string.
     */
    std::string _find_best_version(const std::string& pkg_name, 
                                   const std::vector<std::pair<std::string, std::string>>& constraints);
    
    /**
     * @brief Determines if a package version supports the target architecture or 'all'.
     * @param pkg_name The package name.
     * @param version The package version.
     * @param target_arch The requested architecture.
     * @return std::string The matching architecture string.
     */
    std::string _target_arch_or_all(const std::string& pkg_name, 
                                    const std::string& version, 
                                    const std::string& target_arch);
    
    /**
     * @brief Extracts control file key-value pairs using dpkg-deb.
     * @param file_path Path to the .deb file.
     * @return std::map<std::string, std::string> Map of control fields.
     */
    std::map<std::string, std::string> _get_raw_control_data(const std::string& file_path);
    
    /**
     * @brief Parses Debian dependency strings into logical constraints.
     * @param depends_str Raw dependency string from control file.
     * @return std::map<std::string, std::vector<std::pair<std::string, std::string>>> Map of package constraints.
     */
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> _parse_constraints(const std::string& depends_str);
    
    /**
     * @brief Resolves best version and architecture for a list of dependencies.
     * @param depends_str Raw dependency string.
     * @param target_arch Requested hardware architecture.
     * @return std::vector<Depend> Resolved package arrays.
     */
    std::vector<Depend> _resolve_dependencies(const std::string& depends_str, const std::string& target_arch);

    /**
     * @brief Computes the SHA1 hash of a file.
     * @param file_path Path to the file.
     * @return std::string Hex string of the SHA1 hash.
     */
    std::string _calculate_sha1(const std::string& file_path);

    /**
     * @brief Computes the SHA256 hash of a file.
     * @param file_path Path to the file.
     * @return std::string Hex string of the SHA256 hash.
     */
    std::string _calculate_sha256(const std::string& file_path);

    struct ResolutionContext {
        std::string arch;
        constraint_map global_constraints;
        std::map<std::string, PackageMetadata> resolved_packages;
        std::set<std::string> replaces_provides;
        std::set<std::string> enqueued;
        std::queue<std::string> queue;
        std::vector<std::pair<std::string, std::string>> dependency_links;
    };

    /**
     * @brief Processes version requirements for a package and updates the resolution context.
     * @param ctx The current dependency resolution context.
     * @param pkg The name of the package to process.
     * @param reqs The list of version constraints.
     * @param parent_metadata The metadata of the package requiring this dependency.
     * @param invert If true, reverses the constraint logic for conflict handling.
     * @param is_essential_break If true, indicates a conflict with an essential system package.
     */
    void process_dependency_requirements(
        ResolutionContext& ctx, 
        const std::string& pkg, 
        const std::vector<std::pair<std::string, std::string>>& reqs, 
        const PackageMetadata& parent_metadata, 
        bool invert, 
        bool is_essential_break
    );

    /**
     * @brief Parses package control data to identify and enqueue dependencies and conflicts.
     * @param ctx The current dependency resolution context.
     * @param c_data The raw control data map.
     * @param parent_metadata The metadata of the package being parsed.
     */
    void parse_and_queue_control_data(
        ResolutionContext& ctx, 
        const std::map<std::string, std::string>& c_data, 
        const PackageMetadata& parent_metadata
    );

    /**
     * @brief Iteratively resolves and downloads all packages currently in the resolution queue.
     * @param ctx The current dependency resolution context.
     */
    void resolve_queued_packages(ResolutionContext& ctx);

    /**
     * @brief Removes dependency entries from metadata that could not be successfully resolved.
     * @param ctx The current dependency resolution context.
     */
    void remove_ghost_dependencies(ResolutionContext& ctx);

    /**
     * @brief Establishes metadata links between packages and their confirmed dependencies.
     * @param ctx The current dependency resolution context.
     */
    void link_dependency(ResolutionContext& ctx);

    /**
     * @brief Handles virtual packages by querying reverse provides and returning the real package info.
     * @param name The name of the virtual package.
     * @param parent_arch The architecture of the parent package requiring this virtual package.
     * @return depend A tuple containing the real package name, version, and architecture.
     */
    Depend handle_virtual_packages(const std::string& name, const std::string& parent_arch);

    /**
     * @brief Filters the resolution context to extract final metadata for all dependencies.
     * @param ctx The current dependency resolution context.
     * @param root_package The name of the original package being resolved.
     * @return std::vector<PackageMetadata> A list of metadata objects for resolved dependencies.
     */
    std::vector<PackageMetadata> extract_final_metadata(const ResolutionContext& ctx, const std::string& root_package);
    
    /**
     * @brief Safely removes and recreates the temporary download directory.
     */
    void cleanup_download_path();

    /**
     * @brief Extracts specific ELF tags using readelf.
     * @param path Path to the ELF file.
     * @param tag Tag to grep for (e.g., "SONAME").
     * @return std::vector<std::string> List of matched tag values.
     */
    std::vector<std::string> get_elf_tags(const std::string& path, const std::string& tag);

    /**
     * @brief Reads an archive stream to a temp file and extracts its SONAME.
     * @param a Pointer to the libarchive struct.
     * @return std::optional<std::string> The extracted SONAME if found.
     */
    std::optional<std::string> extract_soname_from_archive(struct archive* a);

};