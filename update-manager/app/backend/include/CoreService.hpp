#include <string>
#include <vector>
#include <map>

// Third-party headers
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "Broadcaster.hpp"
#include "PackageService.hpp"


class CoreService {
private:
    Broadcaster broadcaster;
    PackageFactory factory;

public:
    CoreService();
    
    ~CoreService() = default;

    std::string process_and_broadcast(
        std::string type,
        std::string pkg,
        std::string version,
        std::string arch,
        std::string volume_path
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