#include "Store.hpp"
#include <spdlog/spdlog.h> 

#include "utils.hpp"

void Store::run()
{
    std::filesystem::path processing_dir = this->store_vol / PROCESSING_DIR;
    
    while (true) {
        for (auto& entry : std::filesystem::directory_iterator(this->receiver_vol)) {
            if (!entry.is_regular_file()) 
                throw std::runtime_error("Non-regular file found in receiver directory, expected only regular files!");

            std::filesystem::path processing = processing_dir / entry.path().filename();
            std::vector<std::filesystem::path> created_paths;
            bool error_occurred = false;
            try {
                // atomic claim
                std::filesystem::copy_file(entry.path(), processing, std::filesystem::copy_options::overwrite_existing);
                std::filesystem::remove(entry.path());

                spdlog::info("[STORE] captured new file {}, beging processing", processing.string());

                TarExtractor extractor(processing);
                json manifest = extractor.get_manifest();
                spdlog::info("[STORE] manifest.json extracted successfully");

                std::vector<json> rts_vector = RTS::sort(manifest);

                for(const auto& package : rts_vector){
                    std::string path = package.at("Store_Path").get<std::string>();
                    std::filesystem::path f_path_fs = this->store_vol / path;

                    if (std::filesystem::exists(f_path_fs))
                        continue;

                    std::filesystem::create_directories(f_path_fs);
                    created_paths.push_back(f_path_fs);
                    std::string filename = package.at("Filename").get<std::string>();

                    std::filesystem::rename(
                        processing_dir / filename, 
                        f_path_fs / filename
                    );

                    SLF::build_slf(package, this->store_vol);

                    auto bson_doc = bsoncxx::from_json(package.dump());
                    this->db.collection.insert_one(bson_doc.view());

                    this->update_distributor(package);
                    
                }

            } catch (const std::exception& e) {
                
                spdlog::warn("[STORE] error while processing {}: {}", entry.path().string(), e.what());
                error_occurred = true;
            }

            // reset volume
            try{
                // cleanup created paths
                if (error_occurred){                 
                    for (const auto& p : created_paths) {
                        std::filesystem::remove_all(p);
                    }
                    spdlog::info("[STORE] Cleaned up created paths after error.");
                }

                created_paths.clear();

                std::filesystem::remove_all(processing_dir);
                std::filesystem::create_directories(processing_dir);

                error_occurred = false;
            } catch (const std::filesystem::filesystem_error& e){
                throw std::filesystem::filesystem_error(
                    "[STORE] [CRITICAL ERROR] Failed to cleanup Store Volume",
                    e.path1(),
                    e.code()
                );
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200)); 
    }
}

Store::Store() : db(set_env_var("MONGO_URI"), "packages_db", "packages")
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
        } else {
            auto err = res.error();
            spdlog::error("[STORE] Failed to connect to distributor ({}): {}", this->distributor_path, httplib::to_string(err));
        }
    } catch (const std::exception& e) {
        spdlog::error("[STORE] Exception occurred during upload_update: {}", e.what());
    }
}
