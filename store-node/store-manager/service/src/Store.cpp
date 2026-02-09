#include "Store.hpp"
#include <spdlog/spdlog.h> 

#include "utils.hpp"

void Store::run()
{
    // Ensure processing directory exists
    std::filesystem::path processing_dir = this->store_vol / PROCESSING_DIR;
    std::filesystem::create_directories(processing_dir);

    while (true) {
        std::filesystem::path entry_path;
        bool file_found = false;

        //Locate update tarball
        try {
            for (auto& entry : std::filesystem::directory_iterator(this->receiver_vol)) {
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

        std::filesystem::path processing_file = processing_dir / entry_path.filename();

        try {

            std::filesystem::copy_file(entry_path, processing_file, std::filesystem::copy_options::overwrite_existing);
            std::filesystem::remove(entry_path);

            spdlog::info("[STORE] Captured file {}, extracting...", processing_file.string());

            TarExtractor extractor(processing_file);
            json manifest = extractor.get_manifest();
            
            std::vector<json> package_vector = manifest.at("Packages").get<std::vector<json>>();
            RecipeMaker maker(manifest);

            // process packages loop
            for (const auto& package : package_vector) {
                std::filesystem::path f_path_fs;
                
                try {
                    std::string path = package.at("Store_Path").get<std::string>();
                    f_path_fs = this->store_vol / path;

                    // If already exists, skip.
                    if (std::filesystem::exists(f_path_fs)) {
                        spdlog::info("[STORE] Path {} already exists, skipping.", f_path_fs.string());
                        continue;
                    }

                    std::filesystem::create_directories(f_path_fs);

                    std::string filename = package.at("Filename").get<std::string>();
                    std::filesystem::rename(
                        processing_dir / filename, 
                        f_path_fs / filename
                    );

                    maker.generate_recipe(f_path_fs, package.at("Type").get<std::string>());
                    
                    // Commit to DB 
                    auto bson_doc = bsoncxx::from_json(package.dump());
                    this->db.collection.insert_one(bson_doc.view());

                    this->update_distributor(package);
                    
                    spdlog::info("[STORE] Successfully processed package: {}", filename);

                } catch (const std::exception& e) {
                    // ERROR HANDLER FOR SINGLE PACKAGE
                    spdlog::error("[STORE] Failed to process package inside bundle: {}", e.what());
                    
                    // Cleanup
                    if (!f_path_fs.empty() && std::filesystem::exists(f_path_fs)) {
                        try {
                            std::filesystem::remove_all(f_path_fs);
                        } catch(...) { /* ignore cleanup errors */ }
                    }
                }
            }

        } catch (const std::exception& e) {
            // This catches errors with the Tarball itself (corruption) or the Manifest
            spdlog::error("[STORE] Critical error processing bundle {}: {}", processing_file.string(), e.what());
        }

        // final cleanup: clear the processing directory 
        try {
            std::filesystem::remove_all(processing_dir);
            std::filesystem::create_directories(processing_dir);
        } catch (const std::exception& e) {
             spdlog::error("[STORE] Failed to reset processing directory: {}", e.what());
        }
    }
}

Store::Store() : db(set_env_var("MONGO_URI"), set_env_var("MONGO_PACKAGES_DB"), set_env_var("MONGO_PACKAGES_COLLECTION"))
{
    this->store_vol = set_env_var("STORE_PATH");
    std::filesystem::path output_path = set_env_var("OUTPUT_PATH");

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
