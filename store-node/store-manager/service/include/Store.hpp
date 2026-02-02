#pragma once

#include "Algorithms.hpp"
#include "TarExtractor.hpp"

#include <httplib.h>
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

using json = nlohmann::json;

//database client
struct db_instance {
    mongocxx::instance instance;

    std::unique_ptr<mongocxx::client> client;
    mongocxx::database db;
    mongocxx::collection collection;

    db_instance(const std::string& uri, const std::string& db_name,const std::string& coll_name) : 
        instance{},
        client(std::make_unique<mongocxx::client>(mongocxx::uri{uri})),
        db((*client)[db_name]),
        collection(db[coll_name])
    {}
};

class Store {
private:
    void update_distributor(const json& package_json);

    std::unique_ptr<httplib::Client> cli;

    std::string distributor_path;
    std::filesystem::path store_vol, receiver_vol;

    struct db_instance db;

public:

    void run();

    Store();

    ~Store() = default;

};