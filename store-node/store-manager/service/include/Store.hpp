#pragma once

#include "Algorithms.hpp"
#include "TarExtractor.hpp"

#include <nlohmann/json.hpp> 
#include <filesystem>
#include <thread>
#include <chrono>
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
#include "Recipe.hpp"
#include "PackageReader.hpp"

class Store {
private:
    fs::path store_vol, receiver_vol;

    struct db_instance db;

public:

    void run();

    Store();

    ~Store() = default;

};