#include "CoreService.hpp"

std::string CoreService::process_and_broadcast(
    std::string type,
    std::string pkg,
    std::string version,
    std::string arch,
    std::string volume_path
) {
    spdlog::info("Processing package: {} {} {} {}", type, pkg, version, arch);

    auto engine = this->factory.get_engine(type);
    if (!engine) {
        spdlog::error("Unsupported package type: {}", type);
        return json{{"error", "Unsupported package type"}}.dump();
    }

    engine->init();
    
    PackageMetadata metadata = engine->get_package_metadata(pkg, version, arch);
    std::string main_package_path = engine->get_package_file(pkg, version, arch);
    
    if (metadata.name.empty()) {
        spdlog::error("Failed to retrieve metadata for package: {} {} {} {}", type, pkg, version, arch);
        return json{{"error", "Package metadata not found"}}.dump();
    }

    return this->broadcaster.send(main_package_path, metadata, engine.get(), volume_path).dump();
}

CoreService::CoreService() : broadcaster(), factory() {
    spdlog::info("CoreService initialized");
}

std::vector<std::string> CoreService::get_package_instances(
    std::string pkg,
    std::string type
) {
    auto engine = this->factory.get_engine(type);
    if (!engine) {
        spdlog::error("Unsupported package type: {}", type);
        return json{{"error", "Unsupported package type"}}.dump();
    }

    engine->init();
    std::vector<json> instances = engine->get_package_instances(pkg);
    std::vector<std::string> result;

    for(const auto& instance : instances) {
        result.push_back(instance.dump());
    }

    return result;
}

std::string CoreService::get_package_info(
    std::string type,
    std::string pkg,
    std::string version,
    std::string arch
) {
    auto engine = this->factory.get_engine(type);
    if (!engine) {
        spdlog::error("Unsupported package type: {}", type);
        return json{{"error", "Unsupported package type"}}.dump();
    }

    engine->init();
    json info = engine->get_package_info(pkg, version, arch);
    
    if (info.is_null()) {
        spdlog::error("Package not found: {} {} {} {}", type, pkg, version, arch);
        return json{{"error", "Package not found"}}.dump();
    }

    return info.dump();
}
