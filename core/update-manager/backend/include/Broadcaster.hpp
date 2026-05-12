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

class Broadcaster {

    void assign_target(const std::string& ip);
    
    bool create_file_entry(Target& targ, const std::string& file_path, std::string& out_error) noexcept;
    
    Target target;

    unsigned short mcast_port;

    boost::asio::io_context io;

    std::string update_path;

    std::thread send_thread;

public:

    Broadcaster();

    ~Broadcaster();

    json send(const fs::path& tar_path);
    
};

