#include "Reporter.hpp"

Reporter::Reporter() :
    db_inst{},
    
    pkg_db(get_env_var(env::MONGO_REPLICA_URI), 
        get_env_var(env::MONGO_PACKAGES_DB), 
        get_env_var(env::MONGO_PACKAGES_COLLECTION)
    ),

    report_db(get_env_var(env::MONGO_URI), 
        get_env_var(env::MONGO_REPORTS_DB), 
        get_env_var(env::MONGO_REPORTS_COLLECTION)
    ),

    net_id(get_env_var(env::NET_ID)), 
    netname(get_env_var(env::NETNAME)), 
    subnet(get_env_var(env::SUBNET)), 

    worker_threads(std::thread::hardware_concurrency()) // One worker per CPU core
{}

Reporter::~Reporter() {
    worker_threads.join(); // Wait for pending reports to finish sending
    close(this->udp_sockfd);
}

void Reporter::run() {
    const char* ip_env = get_env_var(env::VIEW_IP);
    const char* port_env = get_env_var(env::VIEW_PORT);

    if (!ip_env || !port_env) {
        spdlog::error("[REPORTER] Environment variables VIEW_IP or VIEW_PORT not set");
        return; 
    }

    if ((this->udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        spdlog::error("[REPORTER] Socket creation failed");
        return;
    }

    memset(&this->servaddr, 0, sizeof(this->servaddr));
    this->servaddr.sin_family = AF_INET;
    this->servaddr.sin_port = htons(std::stoi(port_env));
    
    if (inet_pton(AF_INET, ip_env, &this->servaddr.sin_addr) <= 0) {
        spdlog::error("[REPORTER] Invalid IP address: {}", ip_env);
        return;
    }

    this->handle_incoming_report();
}

void Reporter::handle_incoming_report() {
    spdlog::info("[REPORTER] Change Stream Listener active.");
    std::size_t try_num = 0;
    mongocxx::pipeline pipeline{};
    pipeline.match(make_document(kvp("operationType", "insert")));
    
    while (true) {
        try {
            auto cursor = this->report_db.collection.watch(pipeline);

            for (auto&& event : cursor) {
                bsoncxx::document::view full_doc = event["fullDocument"].get_document().view();
                json report_json = json::parse(bsoncxx::to_json(full_doc));

                boost::asio::post(worker_threads, [this, report_json = std::move(report_json)]() mutable {
                    this->process_and_send_report(std::move(report_json));
                });
            }
        } catch (const std::exception& e) {
            spdlog::warn("[REPORTER] Connection failed (node might not be Primary yet). Retrying in 2 seconds... Error: {}", e.what());
            if(++try_num >= TRY_LIMIT) {
                spdlog::error("[REPORTER] Exceeded maximum retry attempts. Last error: {}", e.what());
                break;
            }
            // Wait 2 seconds before trying to restart the watch stream
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
}

void Reporter::process_and_send_report(json report_json) {
    json filtered_packages = json::array();

    for (auto& pkg : report_json[report::PACKAGES]) {
        // Delete SKIPPED_DUPLICATE
        if (pkg[report::STATUS] == report::STATUS_SKIPPED) {
            continue; 
        }

        // Swap SHA256 for Metadata if SUCCESS
        if (pkg[report::STATUS] == report::STATUS_SUCCESS && pkg.contains(report::SHA256)) {
            std::string sha_val = pkg[report::SHA256].get<std::string>();
            auto filter = make_document(kvp(std::string_view(pkg::SHA256), sha_val));
            
            auto result = this->pkg_db.collection.find_one(filter.view());
            if (result) {
                json metadata = json::parse(bsoncxx::to_json(result->view()));
                metadata.erase("_id"); 
                pkg[report::METADATA] = metadata;
                pkg.erase(report::SHA256); 
            }
        }
        filtered_packages.push_back(pkg);
    }
    
    report_json[report::PACKAGES] = filtered_packages;
    report_json.erase("_id");

    // Add Network Info
    report_json[report::NETWORK][report::NET_ID] = this->net_id;
    report_json[report::NETWORK][report::NETNAME] = this->netname;
    report_json[report::NETWORK][report::SUBNET] = this->subnet;

    // Send UDP
    std::string payload = report_json.dump();
    spdlog::info("Sending UDP report for update: {}", report_json[report::BUNDLE_NAME].get<std::string>());
    sendto(this->udp_sockfd, payload.c_str(), payload.size(), 0, 
           (const struct sockaddr*)&this->servaddr, sizeof(this->servaddr));
}