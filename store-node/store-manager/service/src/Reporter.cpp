#include "Reporter.hpp"

void Reporter::handle_report_health()
{
    //TODO: send reports in a loop to udp socket
}

void Reporter::handle_incoming_report()
{
    //TODO: recive dtatbase stream and update client map acordingly
}

void Reporter::populate_reports()
{
    //TODO: populate the client map with the last known report for each client from the database
}

Reporter::Reporter() : db(set_env_var("MONGO_URI"), set_env_var("MONGO_HEALTH_DB"), set_env_var("MONGO_HEALTH_COLLECTION"))
{}

Reporter::~Reporter()
{
    this->stop();
}

void Reporter::run() {
    const char* ip_env = set_env_var("VIEW_IP");
    const char* port_env = set_env_var("VIEW_PORT");

    if (!ip_env || !port_env) {
        fprintf(stderr, "Error: Environment variables VIEW_IP or VIEW_PORT not set\n");
        return; 
    }

    if ((this->udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        return;
    }

    // Address Setup
    memset(&servaddr, 0, sizeof(servaddr));
    this->servaddr.sin_family = AF_INET;
    this->servaddr.sin_port = htons(std::stoi(port_env));
    
    // set IP
    if (inet_pton(AF_INET, ip_env, &this->servaddr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address: %s\n", ip_env);
        return;
    }

    // OPTIONAL: Connect the UDP socket to the server address.
    // This makes the OS "remember" the destination.
    if (connect(this->udp_sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect failed");
        return;
    }

    // Initialization logic
    this->populate_reports();

    // Threading: Start a thread to handle periodic health reports
    this->reporting_thread = std::thread(&Reporter::handle_report_health, this);

    // Enter dattabase listener loop
    this->handle_incoming_report();
}

void Reporter::stop() {
    keep_running = false;
    if (this->reporting_thread.joinable()) {
        this->reporting_thread.join();
    }
    db.client.reset();
    close(this->udp_sockfd);

} 

