#include "Store.hpp"
#include <spdlog/spdlog.h> 

void Store::run()
{
    // Ensure processing directory exists
    fs::path processing_dir = this->store_vol / PROCESSING_DIR;
    fs::create_directories(processing_dir);

    spdlog::info("[STORE] Store service started, monitoring for incoming updates...");
    while (true) {
        fs::path entry_path;
        bool file_found = false;

        //Locate update tarball
        try {
            for (auto& entry : fs::directory_iterator(this->receiver_vol)) {
                if (entry.is_regular_file()) {
                    entry_path = entry.path();
                    file_found = true;
                    break; 
                } else {
                    spdlog::warn("[STORE] Skipping non-regular file: {}", entry.path().string());
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

        try {
            fs::copy_file(entry_path, processing_file, fs::copy_options::overwrite_existing);
            fs::remove(entry_path);

            spdlog::info("[STORE] Captured file {}, extracting...", processing_file.string());

            TarExtractor extractor(processing_file);
            json manifest = extractor.get_manifest();
            
            std::vector<json> package_vector = manifest.at(manifest::PACKAGES).get<std::vector<json>>();
            std::string type = manifest.at(manifest::TYPE).get<std::string>();

            std::vector<std::pair<fs::path, json>> target_packages;
            std::vector<std::string> target_paths;
            RecipeMaker maker(manifest);

            std::unique_ptr<PackageReader> pkg_reader;
            forest_map forests;

            try {
                for (const auto& package : package_vector) {
                    fs::path f_path_fs;
                        std::string path = package.at(pkg::PATH).get<std::string>();
                        f_path_fs = this->store_vol / path;

                        if (fs::exists(f_path_fs)) {
                            spdlog::info("[STORE] Path {} already exists, skipping.", f_path_fs.string());
                            continue;
                        }

                        fs::create_directories(f_path_fs);

                        std::string filename = package.at(pkg::FILENAME).get<std::string>();
                        fs::path source_path = f_path_fs / filename;

                        fs::rename(processing_dir / filename, source_path);
                        
                        // Add to the list for the next phase
                        target_packages.push_back({f_path_fs, package});
                        target_paths.push_back(source_path.string());

                        spdlog::debug("[STORE] File storage complete for: {}", filename);

                }

                pkg_reader = PackageReader::create(type);
                forests = pkg_reader->generate_forests(target_paths, maker.get_global_sort());

            } catch (const std::exception& e) {
                spdlog::error("[STORE] Physical storage failed at package: {}, stopping update processing.\n", target_paths.back(),e.what());
                for (const auto& [dir_path, package_data] : target_packages) {
                    fs::path f_path_fs = dir_path / package_data.at(pkg::FILENAME).get<std::string>();

                    if (!f_path_fs.empty() && fs::exists(f_path_fs)) 
                        fs::remove_all(f_path_fs);
                }
                continue;
            }
        
            for (const auto& [dir_path, package_data] : target_packages) {
                fs::path file_path = dir_path / package_data.at(pkg::FILENAME).get<std::string>();

                try {
                    // Generate the recipe file in the store
                    maker.generate_recipe(dir_path, *pkg_reader, forests[file_path.string()]);

                    // Commit to DB 
                    auto bson_doc = bsoncxx::from_json(package_data.dump());
                    this->db.collection.insert_one(bson_doc.view());

                    // Notify Distributor
                    this->update_distributor(package_data);
                    
                    spdlog::info("[STORE] Successfully committed package to DB: {}", file_path.string());

                } catch (const std::exception& e) {
                    spdlog::error("[STORE] Metadata/Distributor update failed for {}: {}", file_path.string(), e.what());
                    if (!file_path.empty() && fs::exists(file_path)) {
                        fs::remove_all(file_path);
                    }
                    
                }
            }

        } catch (const std::exception& e) {
            spdlog::error("[STORE] Critical error processing bundle {}: {}", processing_file.string(), e.what());
        }

        // final cleanup: clear the processing directory 
        try {
            fs::remove_all(processing_dir);
            fs::create_directories(processing_dir);
        } catch (const std::exception& e) {
            spdlog::error("[STORE] Failed to reset processing directory: {}", e.what());
        }
    }
}

Store::Store() : db(set_env_var("MONGO_URI"), set_env_var("MONGO_PACKAGES_DB"), set_env_var("MONGO_PACKAGES_COLLECTION"))
{
    this->store_vol = set_env_var("STORE_PATH");
    fs::path output_path = set_env_var("OUTPUT_PATH");

    this->receiver_vol = output_path / READY_PATH;

    std::string url = set_env_var("DISTRIBUTOR_URL");
    std::string method = set_env_var("UPDATE_FILE_REQUEST");

    spdlog::info("[STORE] DISTRIBUTOR_URL, UPDATE_FILE_REQUEST successfully set to: {}, {}", url, method);

    this->distributor_path = method;
    try{
        this->cli = std::make_unique<httplib::Client>(url);

        this->cli->set_connection_timeout(5);
        this->cli->set_read_timeout(5);
    } catch(const std::exception &e){

        spdlog::error("[STORE] Failed to create HTTP client: {}", e.what());
        throw;
    }
}

void Store::update_distributor(const json& package_json){   
    try {
        auto res = this->cli->Post(this->distributor_path, package_json.dump(), "application/json");

        if (res) {
            if (res->status == 200 || res->status == 201) {
                spdlog::info("[STORE] Successfully uploaded update to distributor: {}", this->distributor_path);
            } else {
                spdlog::error("[STORE] Distributor returned error status: {}", res->status);
            }
            spdlog::info("[STORE] Distributor response body: {}", res->body);
        } else {
            auto err = res.error();
            spdlog::error("[STORE] Failed to connect to distributor ({}): {}", this->distributor_path, httplib::to_string(err));
        }
    } catch (const std::exception& e) {
        spdlog::error("[STORE] Exception occurred during upload_update: {}", e.what());
    }
}
