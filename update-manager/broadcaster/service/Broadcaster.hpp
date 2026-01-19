//
// Created by lokitay on 12/23/25.
//

#pragma once
#include <Transmitter.h>
#include <httplib.h>
#include <nlohmann/json.hpp> 
#include <unordered_map>
#include <utility>
#include <optional>
#include <thread>
#include <mutex>
#include <string>

using json = nlohmann::json;

//set Post path for a func
#define SET_PATH(func) "/" #func

struct ft_arguments {
  std::string mcast_target = {};
  bool enable_ipsec = false;
  bool use_gzip = false;
  bool gen_etags = false;
  const char *aes_key = {};
  unsigned short mcast_port = 40085;
  unsigned short mtu = 1500;
  uint32_t rate_limit = 1000;
  uint64_t tsi = 16;
  size_t retransmit_count = 1;
};

struct fileEntry {
    std::shared_ptr<LibFlute::Transmitter::FileDescription> file;
    size_t transmitted_count;
    
    // ADD THESE TWO LINES to keep the strings alive in memory
    std::string kept_path; 
    std::string kept_name;

    // Update constructor to store them
    fileEntry(LibFlute::Transmitter::FileDescription* f, std::string p, std::string n)
        : file(f), transmitted_count(0), kept_path(std::move(p)), kept_name(std::move(n)) {}
};

struct Target {
    ft_arguments args;
    std::unique_ptr<LibFlute::Transmitter> transmitter;
    std::list<fileEntry> files;

    std::atomic<size_t> pending_files{0};
};

class Broadcaster {

    void setup_broadcast_targets();

    void assign_target(ft_arguments& args, const std::string& ip);

    void send_to_target(const std::string& destination_ip);

    void add_destination(const std::string& destination_ip);

    bool remove_destination(const std::string& destination_ip);

    std::optional<json> parse_json_field(const json& j, httplib::Response& res, const std::string& field);

    json send_object(const std::vector<std::string>& destination_ips, const std::vector<std::string>& file_paths);

    json send_object(const std::string& command, const std::vector<std::string>& file_paths);

    json send_command_all(const std::vector<std::string>& file_paths);

    const char* set_env_var(const std::string& name);

    bool create_file_entry(Target& targ, const std::string& file_path, std::string& out_error) noexcept;

    std::unique_ptr<LibFlute::Transmitter> set_transmiter(const ft_arguments& target_args);
    
    std::unordered_map<std::string, Target> targets;

    httplib::Server svr;

    unsigned short mcast_port;

    std::string ips_file_path;

    boost::asio::io_context io;

    std::string update_path;

    std::thread send_thread;

public:

    Broadcaster();

    ~Broadcaster();

    void run();

};
