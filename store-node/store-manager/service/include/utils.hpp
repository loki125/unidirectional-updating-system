#pragma once

#include <string>
#include <cstdlib>
#include <functional>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/builder/stream/document.hpp>

#define READY_PATH "ready"
#define PROCESSING_DIR "processing"

using json = nlohmann::json;
namespace fs = std::filesystem;

using forest_map = std::map<fs::path, std::map<std::string, fs::path>>;
// hash_path : List{provided_name, provided_soname}
using provider_vector = std::vector<std::pair<std::string, std::string>>;
using provider_map = std::map<fs::path, provider_vector>;


namespace pkg {
    constexpr const char* NAME = "Package";
    constexpr const char* VERSION = "Version";
    constexpr const char* FILENAME = "Filename";
    constexpr const char* PATH = "Store_Path";
    constexpr const char* DEPS = "Dependencies";
}

namespace manifest {
    constexpr const char* PACKAGES = "Packages";
    constexpr const char* TYPE = "Type";
    constexpr const char* FILENAME = "manifest.json";
}

namespace recipe {
    constexpr const char* FILENAME = "recipe.json";
    constexpr const char* PACKAGE_NAME = "package_name";
    constexpr const char* VERSION = "version";
    constexpr const char* MOUNT_REQ = "required_mounts";
    constexpr const char* MOUNT_INS = "mount_instructions";
    constexpr const char* SCRIPTS = "scripts";
    constexpr const char* SYMLINK_FOREST = "symlink_forest";
    constexpr const char* PROVIDER_MAP = "provider_map";
}
namespace script {
    constexpr const char* PRE_OVERLAY = "pre_overlay";
    constexpr const char* IN_OVERLAY = "in_overlay";
    constexpr const char* POST_INSTALL = "postinst";
    constexpr const char* PRE_INSTALL = "preinst";
}

//database client
struct db_instance {
    mongocxx::instance instance;
    
    std::unique_ptr<mongocxx::client> client;
    mongocxx::database db;
    mongocxx::collection collection;

    db_instance(const std::string& uri, const std::string& db_name, const std::string& coll_name) : 
        instance{}, client(std::make_unique<mongocxx::client>(mongocxx::uri{uri}))
    {       
        this->db = (*client)[db_name];
        this->collection = db[coll_name];
    }
};

inline const char* set_env_var(const std::string& name){
    const char* var = std::getenv(name.data());
    if (!var) {
        throw std::runtime_error("Environment variable " + name + " is not set.");
    }
    return var;
}

inline std::string exec_command(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) throw std::runtime_error("popen() failed!");
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}
