#pragma once

#include <unordered_map>
#include <utility>
#include <optional>
#include <thread>
#include <string>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <openssl/sha.h>
#include <archive.h>
#include <archive_entry.h>
#include <filesystem>
#include <chrono>
#include <sstream>


// Third-party headers
#include <Transmitter.h>
#include <nlohmann/json.hpp> 
#include <spdlog/spdlog.h>
#include "../utils/base64.h"

#include "PackageFactory.hpp"
#include "PackageService.hpp"
#include "UpdateBuilder.hpp"
#include "utils.hpp"

/**
 * @brief Manages FLUTE multicast broadcasting for update packages.
 */
class Broadcaster {

    /**
     * @brief Configures the transmitter and retransmission logic for a specific IP.
     * @param ip Multicast destination IP address.
     */
    void assign_target(const std::string& ip);
    
    /**
     * @brief Creates and validates a file entry for the FLUTE session.
     * @param targ Reference to the Target configuration.
     * @param file_path Relative path to the file.
     * @param out_error String to store error messages on failure.
     * @return true if entry created successfully, false otherwise.
     */
    bool create_file_entry(Target& targ, const std::string& file_path, std::string& out_error) noexcept;
    
    Target target; ///< Target configuration and transmission state.

    unsigned short mcast_port; ///< Multicast port number.

    boost::asio::io_context io; ///< IO context for asynchronous operations.

    std::string update_path; ///< Base directory path for updates.

    std::thread send_thread; ///< Thread dedicated to running the IO context.

public:

    /**
     * @brief Initializes environment, logs, and starts the background IO thread.
     */
    Broadcaster();

    /**
     * @brief Stops the IO context and joins the background thread.
     */
    ~Broadcaster();

    /**
     * @brief Prepares and broadcasts a package via FLUTE.
     * @param tar_path Filesystem path to the package.
     * @return json Transmission status and message.
     */
    json send(const fs::path& tar_path);
    
};

