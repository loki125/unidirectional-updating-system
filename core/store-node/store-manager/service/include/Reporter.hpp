
#pragma once

#include <string>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <chrono>
#include <arpa/inet.h>
#include <unistd.h>

// Third-party
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <mongocxx/pipeline.hpp>
#include <mongocxx/options/change_stream.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>

using bsoncxx::builder::basic::make_document;
using bsoncxx::builder::basic::kvp;

#include "utils.hpp"  

#define TRY_LIMIT 10

class Reporter {
public:
    Reporter();
    ~Reporter();

    void run();

private:
    mongocxx::instance db_inst;
    struct db_init pkg_db;
    struct db_init report_db;

    std::string net_id;
    std::string netname;
    std::string subnet;

    int udp_sockfd;
    struct sockaddr_in servaddr;

    boost::asio::thread_pool worker_threads;

    void handle_incoming_report();

    void process_and_send_report(nlohmann::json report_json);
};
