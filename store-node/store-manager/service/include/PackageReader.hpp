#pragma once

#include <iostream>
#include <filesystem>
#include <string>
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
    virtual json get_files(const std::string& path) = 0;   // Returns generic list of {src, dst}
    virtual json get_scripts(const std::string& path) = 0; // Returns {pre, in_overlay}
    
    // Factory: Pick the right reader based on file extension
    static std::unique_ptr<PackageReader> create(const std::string& type);
};

class DebReader : public PackageReader {
public:
    fs::path get_pkg_path(const fs::path& directory_path) {
        // Find the package file (agnostic search)
        for (const auto& entry : fs::directory_iterator(directory_path)) {
            if (entry.path().extension() == ".deb") {
                return entry.path();
            }
        }
        throw std::runtime_error("No .deb package found in " + directory_path.string());
    }

    std::string get_name(const std::string& path) override {
        std::string out = exec_command("dpkg-deb -f " + path + " Package");
        out.erase(out.find_last_not_of(" \n\r\t") + 1);
        return out;
    }

    std::string get_version(const std::string& path) override {
        std::string out = exec_command("dpkg-deb -f " + path + " Version");
        out.erase(out.find_last_not_of(" \n\r\t") + 1);
        return out;
    }

    json get_files(const std::string& path) override {
        json forest = json::array();
        std::stringstream ss(exec_command("dpkg-deb -c " + path));
        std::string line;

        while (std::getline(ss, line)) {
            if (line.empty() || line[0] == 'd') continue; // Skip directories
            
            // Parse: "drwxr-xr-x ... ./usr/bin/hello"
            size_t dot_pos = line.find(" ./");
            if (dot_pos == std::string::npos) continue;

            std::string raw = line.substr(dot_pos + 1);
            if (!raw.empty() && raw.back() == '\n') raw.pop_back();

            std::string src = raw;
            std::string dst = raw;

            // Handle internal links "bin/sh -> bash"
            size_t arrow = raw.find(" -> ");
            if (arrow != std::string::npos) {
                src = raw.substr(0, arrow); // Internal file
                // dst stays the same (the location of the link)
            }

            if (src.length() > 0 && src[0] == '/') src = src.substr(1);
            forest.push_back({ {"src", src}, {"dst", dst} });
        }
        return forest;
    }

    json get_scripts(const std::string& path) override {
        json scripts;
        std::vector<std::string> pre, in_ov;
        std::string out = exec_command("dpkg-deb -I " + path);
        
        if (out.find(" preinst ") != std::string::npos) pre.push_back("preinst");
        if (out.find(" postinst ") != std::string::npos) in_ov.push_back("postinst");

        scripts["pre_overlay"] = pre;
        scripts["in_overlay"] = in_ov;
        return scripts;
    }
};

