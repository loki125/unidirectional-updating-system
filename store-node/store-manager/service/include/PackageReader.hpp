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
#include <unistd.h> 
#include <archive.h>
#include <cstdlib>
#include <archive_entry.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "utils.hpp"
#include "Algorithms.hpp"

class PackageReader {
public:
    virtual ~PackageReader() = default;
    
    virtual fs::path get_pkg_path(const fs::path& directory_path) = 0;
    virtual std::string get_name(const std::string& path) = 0;
    virtual std::string get_version(const std::string& path) = 0;
    virtual forest_map generate_forests(const std::vector<std::string>& target_paths, const provider_map& global_provider_map, const GSO& global_sort) = 0;
    virtual provider_map build_provider_map(const std::vector<std::string>& all_store_paths) = 0;
    virtual json get_scripts(const std::string& path) = 0; // Returns {pre, in_overlay}
    
    // Factory: Pick the right reader based on file extension
    static std::unique_ptr<PackageReader> create(const std::string& type);
};

class DebReader : public PackageReader {

private:
    std::vector<std::string> get_elf_tags(const std::string& path, const std::string& tag);

    bool is_system_pkg(const std::string& pkg_name);

public:
    fs::path get_pkg_path(const fs::path& directory_path) override;

    std::string get_name(const std::string& path) override;

    std::string get_version(const std::string& path) override;

    provider_map build_provider_map(const std::vector<std::string>& all_store_paths) override;

    forest_map generate_forests(const std::vector<std::string>& target_paths, const provider_map& global_provider_map, const GSO& global_sort) override ;

    json get_scripts(const std::string& path) override;
};

