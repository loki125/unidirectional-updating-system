#include <nlohmann/json.hpp> 
#include <thread>
#include <mutex>
#include <unistd.h>
#include <unordered_map>
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/builder/stream/document.hpp>

#include "utils.hpp"

class Reporter {
private:
    void handle_report_health();

    void handle_incoming_report();

    void populate_reports();

    // Map MAC Address -> Last known JSON report
    std::unordered_map<std::string, json> client_data;
    
    // Protects access to the map
    std::mutex data_mutex; 
    
    std::thread reporting_thread;

    bool keep_running = true;

    struct db_instance db;

    int32_t udp_sockfd;

    struct sockaddr_in servaddr;



public:
    Reporter();
    ~Reporter();

    void run(); // Starts the processing thread
    void stop();
};

