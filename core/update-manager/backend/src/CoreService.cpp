#include "CoreService.hpp"


std::string CoreService::process_and_broadcast(
    std::string type,
    std::string pkg,
    std::string version,
    std::string arch
) {
    spdlog::info("Processing package: {} {} {} {}", type, pkg, version, arch);

    auto engine = this->factory.get_engine(type);
    if (!engine) {
        spdlog::error("Unsupported package type: {}", type);
        return json{{"error", "Unsupported package type"}}.dump();
    }

    fs::path update_path;
    std::string response_json;

    try {
        engine->init();

        std::string f_file_path = engine->get_package_file(pkg, version, arch);
        PackageMetadata metadata = engine->get_package_metadata(f_file_path);

        update_path = this->update_builder.build(engine.get(), metadata, f_file_path, volume_path); 
        
        response_json = this->broadcaster.send(update_path).dump();

    } catch (const std::exception& e) {
        spdlog::error("Error processing package {}: {}", pkg, e.what());
        response_json = json{{"error", e.what()}}.dump();

        if (!update_path.empty()) {
            std::error_code ec; 
            if (fs::exists(update_path, ec)) {
                fs::remove(update_path, ec);
                if (ec) {
                    spdlog::warn("Failed to delete leftover update_path {}: {}", update_path.string(), ec.message());
                } else {
                    spdlog::info("Cleaned up lingering update_path: {}", update_path.string());
                }
            }
        }
    } catch (...) {
        spdlog::error("Unknown error processing package {}", pkg);
        response_json = json{{"error", "An unknown internal error occurred"}}.dump();
        
        if (!update_path.empty()) {
            std::error_code ec;
            fs::remove(update_path, ec);
        }
    }

    if (!engine->download_path.empty()) {
        std::error_code ec;
        
        fs::remove_all(engine->download_path, ec);
        if (ec) {
            spdlog::warn("Failed to delete download_path {}: {}", engine->download_path.string(), ec.message());
        }
        
        ec.clear();
        fs::create_directories(engine->download_path, ec);
        if (ec) {
            spdlog::warn("Failed to recreate download_path {}: {}", engine->download_path.string(), ec.message());
        }
    }

    return response_json;
}

CoreService::CoreService() : volume_path(get_env_var("UPDATE_FILE_PATH")), broadcaster(), update_builder(), factory(volume_path / PROCESSING) {
    spdlog::info("CoreService initialized");
}

std::vector<std::string> CoreService::get_package_instances(
    std::string pkg,
    std::string type
) {
    std::vector<std::string> result;
    auto engine = this->factory.get_engine(type);
    
    if (engine) {
        engine->init();
        std::vector<json> instances = engine->get_package_instances(pkg);

        for(const auto& instance : instances) {
            result.push_back(instance.dump());
        }
    }
    else{
        spdlog::error("Unsupported package type: {}", type);
        result.push_back(json{{"error", "Unsupported package type"}}.dump());
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
