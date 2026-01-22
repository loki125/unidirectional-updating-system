#include "Store.hpp"
#include <spdlog/spdlog.h> 

//helper
const char* set_env_var(const std::string& name){
    const char* var = std::getenv(name.data());
    if (!var) {
        throw std::runtime_error("Environment variable " + name + " is not set.");
    }
    return var;
}



void Store::run()
{
    while (true) {
        for (auto& entry : std::filesystem::directory_iterator(ready_dir)) {
            if (entry.is_regular_file()) {
                std::filesystem::path processing = this->stor_vol / "processing" / entry.path().filename();
                try {
                    // atomic claim
                    std::filesystem::rename(entry.path(), processing);
                    spdlog::info("captured new file {}, beging processing", processing);

                    TarExtractor::extract(processing);
                    json manifest = TarExtractor::get_manifest();

                    std::vector<std::string> rts_vector = RTS::sort(manifest);

                    for(const auto& f_path : rts_vector){

                        if (!this->hashpath_exists(f_path))
                            this->create_hashpath(f_path)
                    }
                    
                    json main_package = TarExtractor::get_main_package();
                    this->update_distributor(main_package);

                    // reset volume
                    std::filesystem::remove_all(dir);
                    std::filesystem::create_directories(dir);

                } catch (const std::filesystem::filesystem_error& e) {
                    // file might have been claimed already
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // adjust as needed
    }
}

Store::Store()
{
    this->store_vol = set_env_var("STORE_VOLUME");
    this->receiver_vol = set_env_var("OUTPUT_PATH");

    std::string url = set_env_var("DISTRIBUTOR_URL");
    std::string method = set_env_var("UPDATE_FILE_REQUEST");
    spdlog::info("DISTRIBUTOR_URL, UPDATE_FILE_REQUEST successfully set to: {}, {}", url, method);

    this->distributor_path = method;
    try{
        this->cli = std::make_unique<httplib::Client>(url);

        this->cli->set_connection_timeout(5);
        this->cli->set_read_timeout(5);
    } catch(const std::exception &e){
        spdlog::error("Failed to create HTTP client: {}", e.what());
        throw;
    }
}

Store::~Store() {
    this->cli.stop();

    if (fd >= 0)
        close(fd);
}

void Store::update_distributor(const json& package_json){   
    try {
        auto res = this->cli->Post(this->distributor_path, package_json.dump(), "application/json");

        if (res) {
            if (res->status == 200 || res->status == 201) {
                spdlog::info("Successfully uploaded update to distributor: {}", this->distributor_path);
            } else {
                spdlog::error("Distributor returned error status: {}", res->status);
            }
        } else {
            auto err = res.error();
            spdlog::error("Failed to connect to distributor ({}): {}", this->distributor_path, httplib::to_string(err));
        }
    } catch (const std::exception& e) {
        spdlog::error("Exception occurred during upload_update: {}", e.what());
    }
}

bool Store::hashpath_exists(const std::string &path)
{
    return false;
}

void Store::create_hashpath(const std::string &path)
{
}
