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

/**
 * @brief Main service orchestration class for package processing and broadcasting.
 */
class CoreService {
private:
    fs::path volume_path; ///< Base directory path for update storage and processing.

    Broadcaster broadcaster; ///< Service responsible for multicast transmission.
    PackageFactory factory; ///< Factory to retrieve specific package type engines.
    UpdateBuilder update_builder; ///< Service to build final update packages.

public:
    /**
     * @brief Initializes the service, volume paths, and internal components.
     */
    CoreService();
    
    ~CoreService() = default;

    /**
     * @brief Orchestrates package retrieval, building, and multicast broadcasting.
     * @param type The package manager type.
     * @param pkg The name of the package.
     * @param version The specific version to process.
     * @param arch The target architecture.
     * @return std::string JSON response indicating success or error details.
     */
    std::string process_and_broadcast(
        std::string type,
        std::string pkg,
        std::string version,
        std::string arch
    );

    /**
     * @brief Retrieves a list of available versions/instances for a package.
     * @param pkg The name of the package.
     * @param type The package manager type.
     * @return std::vector<std::string> List of JSON strings representing instances.
     */
    std::vector<std::string> get_package_instances(
        std::string pkg,
        std::string type
    );

    /**
     * @brief Gets detailed metadata for a specific package version.
     * @param type The package manager type.
     * @param pkg The name of the package.
     * @param version The package version.
     * @param arch The target architecture.
     * @return std::string JSON string containing package information.
     */
    std::string get_package_info(
        std::string type,
        std::string pkg,
        std::string version,
        std::string arch
    );
};