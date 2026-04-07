#include "Receiver.hpp"

#include <cstdlib>      
#include <stdexcept>    
#include <filesystem>   
#include <spdlog/spdlog.h> 

#include "utils.hpp"

void FluteReceiver::set_receiver()
{
    // Create the receiver
    this->receiver = std::make_unique<LibFlute::Receiver>(
        this->args.flute_interface.data(),
        this->args.mcast_target.data(),
        (short)this->args.mcast_port,
        this->args.tsi,
        this->io);

    // Configure IPSEC, if enabled
    if (this->args.enable_ipsec){
      this->receiver->enable_ipsec(1, this->args.aes_key);
    }

    this->receiver->register_completion_callback(
      [this, output_path = this->args.output_path](std::shared_ptr<LibFlute::File> file) { //NOLINT
        std::filesystem::path out_file, file_name = std::filesystem::path(file->meta().content_location).filename();
        if (!output_path.empty()) 
            out_file = std::filesystem::path(output_path) / file_name;
        
        spdlog::info("{} (TOI {}) has been received", out_file.string(), file->meta().toi);

        try{
            FILE *fd = fopen(out_file.c_str(), "wb");
            fwrite(file->buffer(), 1, file->length(), fd);
            fclose(fd);
            
            std::filesystem::rename(
                out_file, 
                std::filesystem::path(output_path) / READY_PATH / file_name
            );
            spdlog::info("File {} moved to ready path", file_name.string());
        } catch (const std::exception &e){
            spdlog::error("Error while writing file {}: {}", file_name.string(), e.what());
        }
        
      });
}

std::string FluteReceiver::get_receiver_interface_ip() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct ifreq ifr;
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, "eth1", IFNAMSIZ-1); // Force eth1 (mcast-gateway)
    
    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        close(fd);
        spdlog::error("Failed to get IP address for eth1. Defaulting to 0.0.0.0");
        return "0.0.0.0"; // Fallback
    }
    close(fd);
    spdlog::info("Receiver will listen on IP: {}", inet_ntoa(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr));
    return inet_ntoa(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr);
}

FluteReceiver::FluteReceiver()
{
    exec_command("/entrypoint.sh"); // Ensure env vars are set for the receiver
    
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    ft_arguments& args = this->args;
    args.flute_interface = "172.30.1.3"; 

    args.mcast_port = std::stoi(set_env_var("FLUTE_PORT"));
    spdlog::info("FLUTE_PORT successfully set to: {}", args.mcast_port);

    args.mcast_target = set_env_var("FLUTE_IP");
    spdlog::info("FLUTE_IP successfully set to: {}", args.mcast_target);

    args.output_path = set_env_var("OUTPUT_PATH");
    spdlog::info("OUTPUT_PATH successfully set to: {}", args.output_path);

    this->set_receiver();
}

FluteReceiver::~FluteReceiver()
{
    if (!io.stopped()) {
        io.stop();
    }
}

void FluteReceiver::run()
{
    spdlog::info("reciver is up and running");
    this->io.run();
}
