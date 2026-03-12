#pragma once

#include <iostream>
#include <filesystem>
#include <string>
#include <map>
#include <cstdio>
#include <regex>
#include <vector>
#include <memory>
#include <sstream>
#include <nlohmann/json.hpp>

#include "utils.hpp"
#include "Algorithms.hpp"

class PackageReader {
public:
    virtual ~PackageReader() = default;
    
    virtual fs::path get_pkg_path(const fs::path& directory_path) = 0;
    virtual std::string get_name(const std::string& path) = 0;
    virtual std::string get_version(const std::string& path) = 0;
    virtual forest_map generate_forests(const std::vector<std::string>& target_packages, const GSO& global_sort) = 0;
    virtual json get_scripts(const std::string& path) = 0; // Returns {pre, in_overlay}
    
    // Factory: Pick the right reader based on file extension
    static std::unique_ptr<PackageReader> create(const std::string& type);
};

class DebReader : public PackageReader {

private:
    std::vector<std::string> get_elf_tags(const std::string& path, const std::string& tag);

    std::map<std::string, std::string> build_provider_map(const std::vector<std::string>& all_store_paths);

    std::string extract_deb_to_processing(const fs::path& deb_path);

    void cleanup_processing_dirs(const std::vector<std::string>& target_packages);

public:
    fs::path get_pkg_path(const fs::path& directory_path) override;

    std::string get_name(const std::string& path) override;

    std::string get_version(const std::string& path) override;

    forest_map generate_forests(const std::vector<std::string>& target_packages, const GSO& global_sort) override ;

    json get_scripts(const std::string& path) override;
};

