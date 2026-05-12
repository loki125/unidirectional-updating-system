#include <string>
#include <vector>
#include <map>

// Third-party headers
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "Broadcaster.hpp"
#include "PackageFactory.hpp"
#include "UpdateBuilder.hpp"
#include "utils.hpp"

class CoreService {
private:
    fs::path volume_path;

    Broadcaster broadcaster;
    PackageFactory factory;
    UpdateBuilder update_builder;

public:
    CoreService();
    
    ~CoreService() = default;

    std::string process_and_broadcast(
        std::string type,
        std::string pkg,
        std::string version,
        std::string arch
    );

    std::vector<std::string> get_package_instances(
        std::string pkg,
        std::string type
    );

    std::string get_package_info(
        std::string type,
        std::string pkg,
        std::string version,
        std::string arch
    );
};