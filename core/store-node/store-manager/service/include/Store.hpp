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

class Store {
private:
    fs::path store_vol, receiver_vol;

    mongocxx::instance db_inst;
    struct db_init pkg_db;
    struct db_init report_db;

    void _bundle_package(const json& recipe, const json& metadata);

public:

    void run();

    Store();

    ~Store() = default;

};