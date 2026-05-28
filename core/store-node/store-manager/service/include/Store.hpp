#pragma once

#include <nlohmann/json.hpp> 
#include <filesystem>
#include <thread>
#include <chrono>
#include <algorithm>
#include <iostream>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_document;

#include "utils.hpp"
#include "TarExtractor.hpp"

/**
 * @brief Manages the extraction, storage, and database commitment of update packages.
 */
class Store {
private:
    fs::path store_vol; ///< Base directory for persistent package storage.
    fs::path receiver_vol; ///< Directory monitored for new files from the receiver.

    mongocxx::instance db_inst; ///< MongoDB driver instance.
    struct db_init pkg_db; ///< Database connection for package metadata.
    struct db_init report_db; ///< Database connection for storage processing reports.

    /**
     * @brief Hard-links recursive dependencies into a package directory for self-contained bundling.
     * @param recipe JSON containing mount requirements.
     * @param metadata JSON containing package file path information.
     */
    void _bundle_package(const json& recipe, const json& metadata);

public:

    /**
     * @brief Main execution loop that monitors for incoming updates and orchestrates storage.
     */
    void run();

    /**
     * @brief Initializes DB connections and volume paths from environment variables.
     */
    Store();

    ~Store() = default;

};