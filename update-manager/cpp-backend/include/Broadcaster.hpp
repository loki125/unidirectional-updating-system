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

#include "utils.hpp"

using json = nlohmann::json;

class Broadcaster {

    void assign_target(const std::string& ip);

    std::string create_tar_object(const UpdateManifest& manifest, const std::vector<std::string>& file_paths, const std::string& tar_path);
    
    bool create_file_entry(Target& targ, const std::string& file_path, std::string& out_error) noexcept;
    
    Target target;

    unsigned short mcast_port;

    boost::asio::io_context io;

    std::string update_path;

    std::thread send_thread;

public:

    Broadcaster();

    ~Broadcaster();

    json send(const std::string& main_package_path, PackageMetadata& update_metadata, PackageService* engine, const std::string& broadcaster_path);
    
};

