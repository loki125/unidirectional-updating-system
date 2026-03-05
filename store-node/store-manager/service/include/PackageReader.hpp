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

using json = nlohmann::json;
namespace fs = std::filesystem;


class PackageReader {
public:
    virtual ~PackageReader() = default;
    
    virtual fs::path get_pkg_path(const fs::path& directory_path) = 0;
    virtual std::string get_name(const std::string& path) = 0;
    virtual std::string get_version(const std::string& path) = 0;
    virtual std::map<std::string, json> generate_forests(const std::vector<std::string>& target_packages) = 0;
    virtual json get_scripts(const std::string& path) = 0; // Returns {pre, in_overlay}
    
    // Factory: Pick the right reader based on file extension
    static std::unique_ptr<PackageReader> create(const std::string& type);
};

class DebReader : public PackageReader {

private:
    std::string get_elf_tag(const std::string& path, const std::string& tag);

    std::map<std::string, std::string> build_provider_map(const std::vector<std::string>& all_store_paths);

    std::string extract_deb_to_processing(const std::string& deb_path);

    void cleanup_processing_dirs(const std::vector<std::string>& target_packages);

public:
    fs::path get_pkg_path(const fs::path& directory_path) override;

    std::string get_name(const std::string& path) override;

    std::string get_version(const std::string& path) override;

    std::map<std::string, json> generate_forests(const std::vector<std::string>& target_packages) override ;

    json get_scripts(const std::string& path) override;
};

