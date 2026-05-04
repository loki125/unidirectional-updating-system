#include "Store.hpp"
#include <spdlog/spdlog.h> 
#include <chrono>

void Store::run()
{
    // Ensure processing directory exists
    fs::path processing_dir = this->store_vol / PROCESSING_DIR;
    fs::create_directories(processing_dir);

    spdlog::info("[STORE] Store service started, monitoring for incoming updates...");
    while (true) {
        fs::path entry_path;
        bool file_found = false;

        // Locate update tarball
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
            std::unique_ptr<PackageReader> pkg_reader = PackageReader::create(
                manifest.at(manifest::TYPE).get<std::string>()
            );

            std::vector<std::pair<fs::path, json>> target_packages;
            RecipeMaker maker(manifest);
            
            provider_map global_provider_map;
            forest_map forests;

            try {
                // Phase 1: Move new files to store volume
                for (const auto& package : package_vector) {
                    fs::path f_path_fs;
                    std::string path = package.at(pkg::PATH).get<std::string>();
                    f_path_fs = this->store_vol / path;

                    if (fs::exists(f_path_fs)) {
                        spdlog::info("[STORE] pkg {} already exists, skipping storage.", package.at(pkg::FILENAME).get<std::string>());
                        continue;
                    }

                    fs::create_directories(f_path_fs);

                    std::string filename = package.at(pkg::FILENAME).get<std::string>();
                    fs::path source_path = f_path_fs / filename;

                    fs::rename(processing_dir / filename, source_path);
                    
                    target_packages.push_back({f_path_fs, package});
                    spdlog::debug("[STORE] File storage complete for: {}", filename);
                }

                // Phase 2: Collect ALL dependencies for the provider map
                std::vector<std::string> all_required_paths;
                for (const auto& pkg_node : maker.get_global_sort().get_sorted_pkgs()) {
                    fs::path p = this->store_vol / pkg_node.at(pkg::PATH).get<std::string>() / pkg_node.at(pkg::FILENAME).get<std::string>();
                    
                    if (fs::exists(p)) {
                        all_required_paths.push_back(p.string());
                    } else {
                        spdlog::warn("[STORE] Expected dependency missing from disk: {}", p.string());
                    }
                }

                // Build provider map from the full dependency graph
                global_provider_map = pkg_reader->build_provider_map(all_required_paths);
                forests = pkg_reader->generate_forests(global_provider_map, maker.get_global_sort());

            } catch (const std::exception& e) {
                spdlog::error("[STORE] Physical storage/mapping failed, stopping update processing: {}", e.what());
                for (const auto& [dir_path, package_data] : target_packages) {
                    if (!dir_path.empty() && fs::exists(dir_path)) 
                        fs::remove_all(dir_path);
                }
                
                bundle_report[report::OVERALL_STATUS] = report::STATUS_CRIT_PHASE_1;
                bundle_report[report::ERROR_MESSAGE] = e.what();
                throw; 
            }
        
            // Phase 3: DB inserts and Recipe Generation (only for NEW target_packages)
            for (const auto& [dir_path, package_data] : target_packages) {
                std::string filename = package_data.at(pkg::FILENAME).get<std::string>();
                
                json pkg_report;
                pkg_report[report::FILENAME] = filename;
                std::string pkg_sha256 = package_data.value(pkg::SHA256, "UNKNOWN_SHA");

                try {
                    auto path_to_check = package_data.at(pkg::PATH).get<std::string>();

                    maker.generate_recipe(dir_path, *pkg_reader, global_provider_map[path_to_check], forests[path_to_check]);

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
                    spdlog::error("[STORE] Metadata/Distributor update failed for {}: {}", filename, e.what());
                    if (!dir_path.empty() && fs::exists(dir_path)) {
                        fs::remove_all(dir_path);
                    }
                    
                    pkg_report[report::STATUS] = report::STATUS_FAILED;
                    pkg_report[report::ERROR_MESSAGE] = e.what();
                    pkg_report[report::METADATA] = package_data; 
                    fail_count++;
                }

                package_reports.push_back(pkg_report);
            }

            bundle_report[report::PACKAGES] = package_reports;
            if (fail_count == 0) {
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