#pragma once

#include "Algorithms.hpp"
#include "TarExtractor.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp> 
#include <filesystem>
#include <thread>
#include <chrono>
#include <iostream>

using json = nlohmann::json;

class Store {
private:
    void update_distributor(const json& package_json);

    std::unique_ptr<httplib::Client> cli;

    std::string distributor_path;
    std::filesystem::path store_vol, receiver_vol;

public:

    void run();

    Store();

    ~Store() = default;

};