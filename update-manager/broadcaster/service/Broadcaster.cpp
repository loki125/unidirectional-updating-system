//
// Created by lokitay on 12/23/25.
//

#include "Broadcaster.hpp"
#include "../utils/base64.h"
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <openssl/sha.h>
#include <spdlog/spdlog.h>
#include <filesystem>

namespace fs = std::filesystem;

// Helper to standardise error responses
void set_json_response(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

Broadcaster::Broadcaster()
{
    spdlog::set_level(spdlog::level::trace);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    
    // extract targets from env, set defualt_args for each and insert it to targets
    this->mcast_port = std::stoi(this->set_env_var("UDP_PORT"));
    spdlog::info("UDP_PORT successfully set to: {}", this->mcast_port);

    this->ips_file_path = this->set_env_var("IPS_PATH");
    spdlog::info("IPS_PATH successfully set to: {}", this->ips_file_path);

    this->update_path = this->set_env_var("UPDATE_FILE_PATH");
    spdlog::info("UPDATE_FILE_PATH successfully set to: {}", this->update_path);

    this->setup_broadcast_targets();
    spdlog::info("finished setting up the targets");
}

Broadcaster::~Broadcaster()
{
    this->svr.stop();
}

const char* Broadcaster::set_env_var(const std::string& name){
    const char* var = std::getenv(name.data());
    if (!var) {
        throw std::runtime_error("Environment variable " + name + " is not set.");
    }
    return var;
}

void Broadcaster::run(){

    this->svr.Post(SET_PATH(send_object), [this](const httplib::Request& req, httplib::Response& res) {
        try {
            const json& j = json::parse(req.body);
            spdlog::info("JSON content: {}", j.dump());
            auto params_opt = this->parse_json_field(j, res, "param");
            if (!params_opt) return;
            
            const json& params = *params_opt;
            
            auto file_paths_opt = this->parse_json_field(params, res, "file_paths");
            if (!file_paths_opt) return;

            auto ip_dests_opt = this->parse_json_field(params, res, "ip_destinations");
            if (!ip_dests_opt) return;

            if (!file_paths_opt->is_array()) {
                set_json_response(res, 400, {{"error", "file_paths must be an array"}});
                return;
            }
            spdlog::info("1");
            std::vector<std::string> file_paths = file_paths_opt->get<std::vector<std::string>>();
            json result_details;

            if(ip_dests_opt->is_string()){
                std::string command = ip_dests_opt->get<std::string>();
                result_details = this->send_object(command, file_paths);
            }
            else if (ip_dests_opt->is_array()){
                std::vector<std::string> destination_ips = ip_dests_opt->get<std::vector<std::string>>();    
                result_details = this->send_object(destination_ips, file_paths);
            }
            else {
                set_json_response(res, 400, {{"error", "ip_destinations must be an array or string"}});
                return;
            }

            spdlog::info("7");
            // Determine HTTP Status Code based on results
            bool has_errors = !result_details["failed_ips"].empty() || !result_details["file_errors"].empty();
            bool has_success = !result_details["successful_ips"].empty();

            if (has_errors && has_success) {
                res.status = 207; // Multi-Status (some worked, some failed)
            } else if (has_errors && !has_success) {
                res.status = 500; // Total failure (or 404/400 depending on logic)
            } else {
                res.status = 200; // All good
            }
            spdlog::info("8");
            res.set_content(result_details.dump(), "application/json");

        } catch (const json::parse_error& e) {
            set_json_response(res, 400, {{"error", "Invalid JSON body"}});
        } catch (const std::exception& e) {
            set_json_response(res, 500, {{"error", std::string("Internal Error: ") + e.what()}});
        }
    });

    this->svr.Post(SET_PATH(add_destination), [this](const httplib::Request& req, httplib::Response& res) {
        try {
            const json& j = json::parse(req.body);
            auto params_opt = this->parse_json_field(j, res, "param");
            if (!params_opt) return;

            const json& params = *params_opt;
            auto destination_ip_opt = this->parse_json_field(params, res, "ip");
            if (!destination_ip_opt) return;

            std::string destination_ip = destination_ip_opt->get<std::string>();
            
            // Check if already exists in memory to return a specific message
            if (this->targets.find(destination_ip) != this->targets.end()) {
                set_json_response(res, 409, {{"status", "error"}, {"message", "IP already exists"}, {"ip", destination_ip}});
                return;
            }

            this->add_destination(destination_ip);
            set_json_response(res, 200, {{"status", "success"}, {"message", "IP added"}, {"ip", destination_ip}});

        } catch (const std::exception& e) {
             set_json_response(res, 500, {{"status", "error"}, {"message", e.what()}});
        }
    });

    this->svr.Post(SET_PATH(remove_destination), [this](const httplib::Request& req, httplib::Response& res) {
        try {
            const json& j = json::parse(req.body);
            auto params_opt = this->parse_json_field(j, res, "param");
            if (!params_opt) return;

            const json& params = *params_opt;
            auto destination_ip_opt = this->parse_json_field(params, res, "ip");
            if (!destination_ip_opt) return;

            std::string destination_ip = destination_ip_opt->get<std::string>();

            bool removed = this->remove_destination(destination_ip);
            
            if (removed) {
                 set_json_response(res, 200, {{"status", "success"}, {"message", "IP removed"}, {"ip", destination_ip}});
            } else {
                 set_json_response(res, 404, {{"status", "error"}, {"message", "IP not found in list"}, {"ip", destination_ip}});
            }
        } catch (const std::exception& e) {
            set_json_response(res, 500, {{"status", "error"}, {"message", e.what()}});
        }
    });

    spdlog::info("Server listening on port 8080...\n");
    this->svr.listen("0.0.0.0", 8080); 

    if (!svr.is_valid()) 
        throw std::runtime_error("server not valid...\n");
}

void Broadcaster::setup_broadcast_targets(){
    std::ifstream file(this->ips_file_path);
    if (!file.is_open()) 
        throw std::runtime_error("Failed to open file: " + ips_file_path);
    
    spdlog::info("opened file: {} successfully", this->ips_file_path);

    std::string na_ip;    
    ft_arguments args;

    while (std::getline(file, na_ip)){
        if(na_ip.empty()) continue; // skip empty lines
        spdlog::info("assigning target to ip: {}", na_ip);      
        this->assign_target(args, na_ip);
    }
    file.close();
    spdlog::info("broadcaster init completed on port {}, starting listening for requests", mcast_port);
}

void Broadcaster::assign_target(ft_arguments& args, const std::string& ip){
    args.mcast_port = this->mcast_port;

    auto& targ = targets.try_emplace(ip).first->second;
    targ.args = args;
    targ.args.mcast_target = ip;

    targ.transmitter = this->set_transmiter(targ.args);
    spdlog::info("inserted ip {} to targets", ip);
}

std::unique_ptr<LibFlute::Transmitter> Broadcaster::set_transmiter(const ft_arguments& target_args){
    spdlog::info("setting transmitter for ip {}", target_args.mcast_target);
    auto transmitter = std::make_unique<LibFlute::Transmitter>(
        target_args.mcast_target.data(),
        static_cast<short>(target_args.mcast_port),
        target_args.tsi,
        target_args.mtu,
        target_args.rate_limit,
        this->io, 
        std::nullopt, 
        LibFlute::FileDeliveryTable::FDT_NS_DRAFT_2005
    );

    if (target_args.enable_ipsec) {
        transmitter->enable_ipsec(1, target_args.aes_key);
    }

    transmitter->register_completion_callback(
    [this, target_args](uint32_t toi) -> void {
        try {
            // Check if key exists to avoid crash
            if (this->targets.find(target_args.mcast_target) == this->targets.end()) return;

            Target* targ = &this->targets.at(target_args.mcast_target);
            auto& files = targ->files;
            auto* transmitter_ptr = targ->transmitter.get();

            for (auto& f : files) {
                if (f.file->toi() == toi) {
                    spdlog::info("{} (TOI {}) has been transmitted", 
                                 f.file->file_entry().content_location, 
                                 f.file->toi());
                    
                    f.transmitted_count++;
                    if (f.transmitted_count < target_args.retransmit_count) {
                        transmitter_ptr->send(f.file);
                    }
                    return; 
                }
            }
        } catch (...) {
            spdlog::error("Error in completion callback for {}", target_args.mcast_target);
        }
    });

    return transmitter;
}

void Broadcaster::add_destination(const std::string& destination_ip){
    std::ofstream file(this->ips_file_path, std::ios::app);

    if (!file.is_open()) 
        throw std::runtime_error("Failed to open file: " + ips_file_path);

    file << destination_ip << std::endl;
    file.close();

    ft_arguments args;
    this->assign_target(args, destination_ip);
}

bool Broadcaster::remove_destination(const std::string& destination_ip){
    std::ifstream in(this->ips_file_path);
    std::ofstream out("tmp.txt"); 
    
    if (!in.is_open() || !out.is_open())
        throw std::runtime_error("Failed to open file for reading/writing");

    std::string line;
    bool found = false;
    while (std::getline(in, line)) {
        if (line == destination_ip){   
            found = true;
            continue;
        }
        out << line << '\n';
    }

    in.close();
    out.close();

    if (std::rename("tmp.txt", this->ips_file_path.data()) != 0) 
        spdlog::error("Failed to rename tmp.txt to " + this->ips_file_path);

    if(!found){
        spdlog::warn("ip: " + destination_ip + " does not exist in " + ips_file_path);
    }

    auto it = this->targets.find(destination_ip);
    if (it != this->targets.end()) {
        this->targets.erase(it);
        found = true; // It existed in memory at least
    }

    return found;
}

json Broadcaster::send_object(const std::vector<std::string>& destination_ips, const std::vector<std::string>& file_paths) {
    
    json response;
    spdlog::info("4");
    response["successful_ips"] = json::array();
    response["failed_ips"] = json::array();
    response["file_errors"] = json::array();

    try {
        for (const auto& ip : destination_ips) {

            // Check Target Existence
            auto it = this->targets.find(ip);
            if (it == targets.end()) {
                spdlog::error("Target {} not found, skipping", ip);
                response["failed_ips"].push_back({
                    {"ip", ip}, 
                    {"reason", "Target IP not configured/found"}
                });
                continue;
            }

            auto& target = it->second;
            if (!target.transmitter) {
                spdlog::error("Target {} has no transmitter, skipping", ip);
                response["failed_ips"].push_back({
                    {"ip", ip}, 
                    {"reason", "Transmitter not initialized"}
                });
                continue;
            }

            // Process Files for this Target
            bool all_files_ok = true;
            target.files.clear(); // Ensure clean slate

            for (const auto& path : file_paths) {
                std::string error_msg;
                spdlog::info("5");
                if (!this->create_file_entry(target, path, error_msg)) {
                    all_files_ok = false;
                    spdlog::error("Failed to add file {} for IP {}: {}", path, ip, error_msg);
                    
                    // Add to global file errors
                    response["file_errors"].push_back({
                        {"ip", ip},
                        {"file", path},
                        {"error", error_msg}
                    });
                }
            }

            // Only attempt send if files were added successfully
            if (!target.files.empty()) {
                try {
                    spdlog::info("6");
                    this->send_to_target(ip);
                    response["successful_ips"].push_back(ip);
                } catch (const std::exception& e) {
                     response["failed_ips"].push_back({
                        {"ip", ip}, 
                        {"reason", std::string("Transmission failed: ") + e.what()}
                    });
                }
                target.files.clear();
            } else throw std::runtime_error("func logic failed");// Logic error.
            
        }
    } catch(const std::exception &e) {
        spdlog::error("Unexpected error at sending object: {}", e.what());
        response["global_error"] = e.what();
    }

    return response;
}

json Broadcaster::send_object(const std::string& command, const std::vector<std::string>& file_paths) {
    spdlog::info("2");
    if(command == "all")
        return this->send_command_all(file_paths);

    // Return error json
    return {
        {"error", "Unknown command"}, 
        {"failed_ips", json::array()}, 
        {"successful_ips", json::array()}, 
        {"file_errors", json::array()}
    };
}

json Broadcaster::send_command_all(const std::vector<std::string>& file_paths){
    // Collect all current keys from map
    spdlog::info("3");
    std::vector<std::string> all_ips;
    for(const auto& pair : this->targets) {
        all_ips.push_back(pair.first);
    }
    return this->send_object(all_ips, file_paths);
}


void Broadcaster::send_to_target(const std::string& destination_ip){

    Target* targ = &this->targets.at(destination_ip);
    LibFlute::Transmitter* transmitter = targ->transmitter.get();

    // Queue all the files
    for (const auto& file : targ->files) {

        // DEBUG CHECK
        if (!file.file) {
             spdlog::error("File pointer is null!"); 
             continue; 
        }
        
        // Print details to verify data is readable before sending
        try {
            const auto &fe = file.file->file_entry();
            spdlog::info("Sending TOI: {}, URI: {}, Type: {}", 
                file.file->toi(), 
                fe.content_location, // If this crashes, the Name string is dead
                fe.content_type      // If this crashes, Type is dead
            );
        } catch (const std::exception& e) {
            spdlog::error("Crash while reading file properties: {}", e.what());
            continue;
        }

        auto toi = transmitter->send( file.file );
        if(toi == -1)
            throw std::runtime_error("sending to destination: " + destination_ip + "failed");
        
    }

    // Start the io_context
    io.restart();
    io.run();
}he targets
um-broadcaster  | [2026-01-18 08:13:41.859] [info] Server listening on p

bool Broadcaster::create_file_entry(Target& targ, const std::string& file_path, std::string& out_error) noexcept {
    
    ft_arguments arguments = targ.args;
    
    try {
        // 1. Prepare paths
        fs::path base_dir = this->update_path;
        fs::path full_path_obj = base_dir / file_path;
        
        // 2. Validate
        if (!fs::exists(full_path_obj) || !fs::is_regular_file(full_path_obj)) {
            out_error = "File not found: " + full_path_obj.string();
            spdlog::error(out_error);
            return false;
        }

        // 3. STORE STRINGS FIRST
        // We emplace the entry into the list first. We pass 'nullptr' for the FileDescription 
        // initially, but we move the path strings into their permanent home in the list.
        targ.files.emplace_back(nullptr, full_path_obj.string(), file_path);

        // 4. GET STABLE REFERENCE
        // Now we get a reference to the entry we just created. 
        // Since it is a std::list, this address will not change.
        auto& entry = targ.files.back();

        // 5. Create FileDescription using the STABLE strings
        // We use entry.kept_path.c_str() which points to the heap memory inside the list node
        auto fd = new LibFlute::Transmitter::FileDescription(
            entry.kept_path.c_str(), 
            entry.kept_name.c_str()
        );

        if (fd->data() == nullptr) {
            delete fd;
            targ.files.pop_back(); // Remove the incomplete entry
            out_error = "LibFlute failed to load file data";
            spdlog::error(out_error);
            return false;
        }

        fd->set_content_type("application/octet-stream");
        fd->set_expiry_time(std::chrono::system_clock::now() + std::chrono::seconds(60));
        
        if (arguments.use_gzip) 
            fd->set_compression(LibFlute::Transmitter::FileDescription::COMPRESSION_GZIP);
        
        if (arguments.gen_etags) {
            std::array<unsigned char, SHA_DIGEST_LENGTH> digest;
            SHA1(reinterpret_cast<const unsigned char*>(fd->data()), fd->data_length(), digest.data());
            fd->set_etag(base64_encode(digest.data(), SHA_DIGEST_LENGTH));
        } else {
             fd->set_etag(""); 
        }

        // 6. Assign the FileDescription to the entry
        // The unique_ptr/shared_ptr will take ownership here
        entry.file.reset(fd);

        return true;

    } catch(const std::exception &e) {
        out_error = e.what(); 
        spdlog::warn("Error creating fileEntry: {}", e.what());
        return false;
    }
}

std::optional<json> Broadcaster::parse_json_field(const json& j, httplib::Response& res, const std::string& field){
    try {
        if (!j.contains(field)) {
            set_json_response(res, 400, {{"error", "missing field: " + field}});
            return std::nullopt;
        }
        return j[field];
    } catch (const json::parse_error&) {
        set_json_response(res, 400, {{"error", "invalid JSON format"}});
        return std::nullopt;
    }
}