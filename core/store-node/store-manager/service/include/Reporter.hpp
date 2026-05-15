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

/**
 * @brief Listens for database changes and sends enriched reports via UDP.
 */
class Reporter {
public:
    /**
     * @brief Initializes database connections, network metadata, and the worker thread pool.
     */
    Reporter();

    /**
     * @brief Joins worker threads and closes the UDP socket.
     */
    ~Reporter();

    /**
     * @brief Configures the UDP socket and starts the MongoDB change stream listener.
     */
    void run();

private:
    mongocxx::instance db_inst; ///< MongoDB driver instance.
    struct db_init pkg_db; ///< Connection to the packages metadata database.
    struct db_init report_db; ///< Connection to the local reports database.

    std::string net_id; ///< Network ID identifier.
    std::string netname; ///< Network name identifier.
    std::string subnet; ///< Subnet identifier.

    int udp_sockfd; ///< Socket file descriptor for UDP transmission.
    struct sockaddr_in servaddr; ///< Destination address configuration for UDP.

    boost::asio::thread_pool worker_threads; ///< Thread pool for processing and sending reports asynchronously.

    /**
     * @brief Watches the MongoDB collection for new inserts using a change stream.
     */
    void handle_incoming_report();

    /**
     * @brief Filters report data, enriches it with package metadata, and transmits via UDP.
     * @param report_json The raw report data from the database.
     */
    void process_and_send_report(nlohmann::json report_json);
};