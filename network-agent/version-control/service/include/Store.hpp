#pragma once

#include "Algorithms.hpp"
#include "RecipeBuilder.hpp"
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

    bool hashpath_exists(const std::string& path);
    
    void create_hashpath(const std::string& path);

    std::unique_ptr<httplib::Client> cli;

    std::string distributor_path, store_vol, receiver_vol;

public:

    void run();

    Store();

    ~Store();

};