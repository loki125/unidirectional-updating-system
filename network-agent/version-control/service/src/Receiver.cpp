#include "Receiver.hpp"

#include <cstdlib>      
#include <stdexcept>    
#include <filesystem>   
#include <spdlog/spdlog.h> 


const char* set_env_var(const std::string& name){
    const char* var = std::getenv(name.data());
    if (!var) {
        throw std::runtime_error("Environment variable " + name + " is not set.");
    }
    return var;
}

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
    if (this->args.enable_ipsec)
    {
      this->receiver->enable_ipsec(1, this->args.aes_key);
    }

    this->receiver->register_completion_callback(
      [this, output_path = this->args.output_path](std::shared_ptr<LibFlute::File> file) { //NOLINT
        std::string out_file = file->meta().content_location;
        if (!output_path.empty()) 
            out_file = (std::filesystem::path(output_path) / std::filesystem::path(out_file).filename()).string();
        
        spdlog::info("{} (TOI {}) has been received", out_file, file->meta().toi);
        FILE *fd = fopen(out_file.c_str(), "wb");
        fwrite(file->buffer(), 1, file->length(), fd);
        fclose(fd);
      });
}

FluteReceiver::FluteReceiver()
{

    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    ft_arguments& args = this->args;
    args.flute_interface = "0.0.0.0"; // Lioutput_pathsten on all interfaces

    args.mcast_port = std::stoi(set_env_var("FLUTE_PORT"));
    spdlog::info("FLUTE_PORT successfully set to: {}", args.mcast_port);

    args.mcast_target = set_env_var("IP");
    spdlog::info("IP successfully set to: {}", args.mcast_target);

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
    this->io.run();
    spdlog::info("reciver is up and running");
}
