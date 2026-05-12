#pragma once

#include <string>
#include <cstdlib>
#include <functional>
#include <tuple>
#include <set>

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
using provider_vector = std::vector<std::tuple<std::string, std::string, bool>>; // provided_name, provided_soname, is_executable
using provider_map = std::map<fs::path, provider_vector>;


namespace pkg {
    constexpr const char* NAME = "Package";
    constexpr const char* VERSION = "Version";
    constexpr const char* FILENAME = "Filename";
    constexpr const char* PATH = "Store_Path";
    constexpr const char* SHA256 = "SHA256";
    constexpr const char* DEPS = "Dependencies";
    constexpr const char* RECIPE = "Recipe";
}
namespace report {
    constexpr const char* BUNDLE_NAME = "bundle_name";
    constexpr const char* TIMESTAMP = "timestamp";
    constexpr const char* PACKAGES = "packages";
    constexpr const char* OVERALL_STATUS = "overall_status";
    constexpr const char* ERROR_MESSAGE = "error_message";
    constexpr const char* FILENAME = "filename";
    constexpr const char* SHA256 = "sha256";
    constexpr const char* STATUS = "status";
    constexpr const char* METADATA = "metadata";
    constexpr const char* NETWORK = "network";
    constexpr const char* NET_ID = "net_id";
    constexpr const char* NETNAME = "network_name";
    constexpr const char* SUBNET = "subnet";

    constexpr const char* STATUS_SUCCESS = "SUCCESS";
    constexpr const char* STATUS_SKIPPED = "SKIPPED_DUPLICATE";
    constexpr const char* STATUS_FAILED = "FAILED";
    constexpr const char* STATUS_PARTIAL = "PARTIAL";
    constexpr const char* STATUS_CRIT_PHASE_1 = "CRITICAL_FAILURE_PHASE_1";
    constexpr const char* STATUS_CRITICAL = "CRITICAL_FAILURE";
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
    constexpr const char* MOUNT_SYS = "system_mounts";
    constexpr const char* MOUNT_INS = "mount_instructions";
    constexpr const char* STATUS = "status";
    constexpr const char* SYMLINK_FOREST = "symlink_forest";
    constexpr const char* PROVIDER_MAP = "provider_map";
    constexpr const char* IS_SYSTEM = "is_system";
}

inline const std::set<std::string> SKIP_FIELDS = {
    recipe::STATUS
};

namespace env {
    // MongoDB Environment Variable Keys
    constexpr const char* MONGO_URI = "MONGO_ISOLATED_URI";
    constexpr const char* MONGO_REPLICA_URI = "MONGO_ISOLATED_REPLICA_URI";
    
    constexpr const char* MONGO_PACKAGES_DB = "MONGO_PACKAGES_DB";
    constexpr const char* MONGO_PACKAGES_COLLECTION = "MONGO_PACKAGES_COLLECTION";
    
    constexpr const char* MONGO_REPORTS_DB = "MONGO_REPORTS_DB";
    constexpr const char* MONGO_REPORTS_COLLECTION = "MONGO_REPORTS_COLLECTION";
    
    // Network Environment Variable Keys
    constexpr const char* VIEW_IP = "VIEW_IP";
    constexpr const char* VIEW_PORT = "VIEW_UDP_PORT";
    constexpr const char* NET_ID = "NETWORK";
    constexpr const char* NETNAME = "NETNAME";
    constexpr const char* SUBNET = "SUBNET";
}

//database client
struct db_init {
    
    std::unique_ptr<mongocxx::client> client;
    mongocxx::database db;
    mongocxx::collection collection;

    db_init(const std::string& uri, const std::string& db_name, const std::string& coll_name) : 
        client(std::make_unique<mongocxx::client>(mongocxx::uri{uri}))
    {       
        this->db = (*client)[db_name];
        this->collection = db[coll_name];
    }
};

inline const char* get_env_var(const std::string& name){
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

inline void sanitize_store_paths(json& j) {
    if (j.is_string()) {
        std::string s = j.get<std::string>();
        
        if (!s.empty() && s[0] == '{') {
            try {
                json nested = json::parse(s);
                sanitize_store_paths(nested); 
                j = nested.dump();            
                return;
            } catch (...) {}
        }
        std::replace(s.begin(), s.end(), ':', '_');
        j = s;
    } 
    else if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (SKIP_FIELDS.find(it.key()) != SKIP_FIELDS.end()) {
                continue; 
            }            
            sanitize_store_paths(it.value()); 
        }
    } 
    else if (j.is_array()) {
        for (auto& element : j) {
            sanitize_store_paths(element); 
         }
    }
}

