#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <future>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "PackageService.hpp"

class PackageFactory {
public:
    PackageFactory(fs::path download_dir) {
        fs::create_directories(download_dir);

       _engine_creators["DebianPackageService"] = [download_dir]() -> std::unique_ptr<PackageService> {
            return std::make_unique<DebianPackageService>(download_dir);
        };
    }

    std::unique_ptr<PackageService> get_engine(const std::string& pack_type) {
        std::string class_name = pack_type + "PackageService";
        auto it = _engine_creators.find(class_name);

        if (it == _engine_creators.end()) {
            spdlog::error("Package service type '{}' not supported", pack_type);
            return nullptr;
        }

        return it->second();
    }

private:
    std::map<std::string, std::function<std::unique_ptr<PackageService>()>> _engine_creators;
};