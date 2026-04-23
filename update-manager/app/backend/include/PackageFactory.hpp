#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <future>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "PackageService.hpp"

class PackageFactory {
public:
    PackageFactory() {
        _engine_creators["DebianPackageService"] = []() -> std::unique_ptr<PackageService> {
            return std::make_unique<DebianPackageService>();
        };
    }

    std::unique_ptr<PackageService> get_engine(const std::string& pack_type) {
        std::string class_name = pack_type + "PackageService";
        auto it = _engine_creators.find(class_name);

        if (it == _engine_creators.end()) {
            return nullptr;
        }

        return it->second();
    }

private:
    std::map<std::string, std::function<std::unique_ptr<PackageService>()>> _engine_creators;
};