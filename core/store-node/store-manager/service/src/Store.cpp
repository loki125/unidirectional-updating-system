#include "Store.hpp"
#include <spdlog/spdlog.h> 
#include <chrono>

void Store::_bundle_package(const json &recipe, const json &metadata)
{
    std::string filename = metadata.at(pkg::FILENAME).get<std::string>();
    
    std::string path = metadata.at(pkg::PATH).get<std::string>();
    fs::path target_pkg_dir = this->store_vol / path;

    try{
        std::vector<fs::path> bundled_mounts = recipe[recipe::MOUNT_INS][recipe::MOUNT_REQ].get<std::vector<fs::path>>();

        for(auto& mount_path : bundled_mounts){
            fs::path f_mount_path = this->store_vol / mount_path;
            if(!fs::exists(f_mount_path))
                throw fs::filesystem_error(
                    "bundled mount not found", 
                    f_mount_path, 
                    std::make_error_code(std::errc::no_such_file_or_directory)
                );            
            for (auto& entry : fs::directory_iterator(f_mount_path)) {
                if (!entry.is_regular_file()) 
                    continue;
                
                fs::path source_entry = entry.path();
                if(source_entry.filename().string() == recipe::FILENAME)
                    continue;

                fs::path destination = target_pkg_dir / source_entry.filename();

                if (fs::exists(destination)) {
                    spdlog::debug("File {} already exists in bundle, skipping", destination.filename().string());
                    continue;
                }

                if (fs::is_symlink(source_entry)) {
                    fs::copy_symlink(source_entry, destination);
                    spdlog::debug("Copied symlink {} to {}", source_entry.filename().string(), destination.string());
                    continue;
                }

                fs::create_hard_link(source_entry, destination);
                spdlog::debug("Hard-linked {} into bundle", source_entry.filename().string());       
            }
        }
    } catch (const fs::filesystem_error& e) {
        throw std::runtime_error("Failed to bundle dependency " + filename + ":\n" + std::string(e.what()));
    } catch (const json::exception& e) {
        spdlog::error("JSON error while bundling package\n{}", recipe.dump(4));
        throw std::runtime_error("JSON error while bundling package " + filename + ":\n" + std::string(e.what()));
    }
    spdlog::debug("Successfully bundled all recursive dependencies into {}", filename);
}

void Store::run()
{
    fs::path processing_dir = this->store_vol / PROCESSING_DIR;
    fs::create_directories(processing_dir);

    spdlog::info("[STORE] Store service started, monitoring for incoming updates...");
    while (true) {
        fs::path entry_path;
        bool file_found = false;

        try {
            for (auto& entry : fs::directory_iterator(this->receiver_vol)) {
                if (entry.is_regular_file()) {
                    entry_path = entry.path();
                    file_found = true;
                    break; 
                } else {
                    spdlog::warn("[STORE] Skipping non-regular file: {}", entry.path().filename().string());
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("[STORE] Error accessing receiver directory: {}", e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

        if (!file_found) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        fs::path processing_file = processing_dir / entry_path.filename();

        json bundle_report;
        bundle_report[report::BUNDLE_NAME] = processing_file.filename().string();
        bundle_report[report::TIMESTAMP] = std::chrono::duration_cast<std::chrono::seconds>(
                                              std::chrono::system_clock::now().time_since_epoch()).count();
        
        json package_reports = json::array();
        int success_count = 0;
        int fail_count = 0;
        int skip_count = 0;

        try {
            fs::copy_file(entry_path, processing_file, fs::copy_options::overwrite_existing);
            fs::remove(entry_path);

            spdlog::info("[STORE] Captured file {}, extracting...", processing_file.filename().string());

            TarExtractor extractor(processing_file);
            json manifest = extractor.get_manifest();
            
            std::vector<json> package_vector = manifest.at(manifest::PACKAGES).get<std::vector<json>>();
            std::vector<std::pair<fs::path, json>> target_packages;
            bool phase1_interrupted = false;

            try {
                for (auto& package : package_vector) {
                    fs::path original_filename = package.value(pkg::FILENAME, "");
                    sanitize_store_paths(package);
                    
                    std::string path = package.at(pkg::PATH).get<std::string>();
                    fs::path f_path_fs = this->store_vol / path;

                    if (fs::exists(f_path_fs)) {
                        spdlog::info("[STORE] pkg {} already exists, skipping storage.", package.at(pkg::FILENAME).get<std::string>());
                        continue;
                    }

                    fs::create_directories(f_path_fs);

                    std::string filename = package.at(pkg::FILENAME).get<std::string>();
                    fs::rename(processing_dir / original_filename, f_path_fs / filename);

                    target_packages.push_back({f_path_fs, package});
                    spdlog::debug("[STORE] File storage complete for: {}", filename);
                }
            } catch (const std::exception& e) {
                spdlog::error("[STORE] Phase 1 mapping interrupted: {}. Proceeding to commit valid packages.", e.what());
                phase1_interrupted = true;
            }
        
            bool cascade_failure = false;
            std::string cascade_error = "";

            for (auto& [dir_path, package_data] : target_packages) {
                std::string filename = package_data.at(pkg::FILENAME).get<std::string>();
                std::string pkg_sha256 = package_data.value(pkg::SHA256, "UNKNOWN_SHA");
                
                json pkg_report;
                pkg_report[report::FILENAME] = filename;

                if (cascade_failure) {
                    if (!dir_path.empty() && fs::exists(dir_path)) {
                        fs::remove_all(dir_path);
                    }
                    pkg_report[report::STATUS] = report::STATUS_FAILED;
                    pkg_report[report::ERROR_MESSAGE] = "Aborted due to previous failure: " + cascade_error;
                    pkg_report[report::METADATA] = package_data; 
                    package_reports.push_back(pkg_report);
                    fail_count++;
                    continue;
                }

                try {
                    std::string recipe_raw = std::move(package_data.at(pkg::RECIPE).get<std::string>());
                    package_data.erase(pkg::RECIPE);

                    json recipe = json::parse(recipe_raw);
                    std::ofstream out(dir_path / recipe::FILENAME);
                    out << recipe.dump(4);
                    out.close();

                    if(recipe[recipe::IS_SYSTEM])
                        this->_bundle_package(recipe, package_data);

                    auto path_to_check = package_data.at(pkg::PATH).get<std::string>();
                    auto filter = make_document(kvp(std::string_view(pkg::PATH), path_to_check));
                    
                    if (this->pkg_db.collection.find_one(filter.view())) {
                        pkg_report[report::STATUS] = report::STATUS_SKIPPED;
                        pkg_report[report::SHA256] = pkg_sha256; 
                        package_reports.push_back(pkg_report);
                        skip_count++;
                        continue; 
                    }

                    auto bson_doc = bsoncxx::from_json(package_data.dump());
                    this->pkg_db.collection.insert_one(bson_doc.view());
                    
                    spdlog::info("[STORE] Successfully committed package to DB: {}", filename);
                    
                    pkg_report[report::STATUS] = report::STATUS_SUCCESS;
                    pkg_report[report::SHA256] = pkg_sha256; 
                    success_count++;

                } catch (const std::exception& e) {
                    spdlog::error("[STORE] Processing failed for {}: {}", filename, e.what());
                    if (!dir_path.empty() && fs::exists(dir_path)) {
                        fs::remove_all(dir_path);
                    }
                    
                    pkg_report[report::STATUS] = report::STATUS_FAILED;
                    pkg_report[report::ERROR_MESSAGE] = e.what();
                    pkg_report[report::METADATA] = package_data; 
                    fail_count++;

                    cascade_failure = true;
                    cascade_error = filename;
                }

                package_reports.push_back(pkg_report);
            }

            bundle_report[report::PACKAGES] = package_reports;
            if (fail_count == 0 && !phase1_interrupted) {
                bundle_report[report::OVERALL_STATUS] = report::STATUS_SUCCESS;
            } else if (success_count == 0 && skip_count == 0) {
                bundle_report[report::OVERALL_STATUS] = report::STATUS_FAILED;
            } else {
                bundle_report[report::OVERALL_STATUS] = report::STATUS_PARTIAL;
            }

        } catch (const std::exception& e) {
            spdlog::error("[STORE] Critical error processing bundle {}: {}", processing_file.string(), e.what());
            if (!bundle_report.contains(report::OVERALL_STATUS)) {
                bundle_report[report::OVERALL_STATUS] = report::STATUS_CRITICAL;
                bundle_report[report::ERROR_MESSAGE] = e.what();
            }
        }

        try {
            auto report_bson = bsoncxx::from_json(bundle_report.dump());
            this->report_db.collection.insert_one(report_bson.view());
            spdlog::info("[STORE] Processing report committed to DB. Status: {}", bundle_report[report::OVERALL_STATUS].get<std::string>());
        } catch (const std::exception& e) {
            spdlog::error("[STORE] Failed to insert report to report_db: {}", e.what());
        }

        try {
            fs::remove_all(processing_dir);
            fs::create_directories(processing_dir);
        } catch (const std::exception& e) {
            spdlog::error("[STORE] Failed to reset processing directory: {}", e.what());
        }
    }
}

Store::Store() : 
    db_inst{},

    pkg_db(get_env_var(env::MONGO_URI), 
           get_env_var(env::MONGO_PACKAGES_DB), 
           get_env_var(env::MONGO_PACKAGES_COLLECTION)),

    report_db(get_env_var(env::MONGO_URI), 
              get_env_var(env::MONGO_REPORTS_DB), 
              get_env_var(env::MONGO_REPORTS_COLLECTION))
{
    this->store_vol = get_env_var("STORE_PATH");
    fs::path output_path = get_env_var("OUTPUT_PATH");

    this->receiver_vol = output_path / READY_PATH;
}