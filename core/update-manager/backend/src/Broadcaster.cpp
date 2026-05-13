#include "Broadcaster.hpp"

void Broadcaster::assign_target(const std::string& ip){
    this->target.args.mcast_port = this->mcast_port;
    this->target.args.mcast_target = ip;

    const ft_arguments& target_args = this->target.args;

    spdlog::info("setting transmitter for ip {}", target_args.mcast_target);
    this->target.transmitter = std::make_unique<LibFlute::Transmitter>(
        target_args.mcast_target.data(),
        static_cast<short>(target_args.mcast_port),
        target_args.tsi,
        target_args.mtu,
        target_args.rate_limit,
        this->io, 
        std::nullopt, 
        LibFlute::FileDeliveryTable::FDT_NS_DRAFT_2005
    );

    this->target.transmitter->register_completion_callback(
    [this, target_args](uint32_t toi) -> void {
        try {

            Target* targ = &this->target;
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
                }
            }
            if (--targ->pending_files == 0) {
                spdlog::info("All files sent to {}", target_args.mcast_target);
                targ->files.clear();
            }
        } catch (...) {
            spdlog::error("Error in completion callback for {}", target_args.mcast_target);
        }
    });

    spdlog::info("inserted ip {} to target", ip);
}

bool Broadcaster::create_file_entry(Target& targ, const std::string& tar_path, std::string& out_error) noexcept {
    
    ft_arguments arguments = targ.args;
    
    try {
        fs::path base_dir = this->update_path;
        fs::path full_path_obj = base_dir / tar_path;
        
        if (!fs::exists(full_path_obj) || !fs::is_regular_file(full_path_obj)) {
            out_error = "File not found: " + full_path_obj.string();
            spdlog::error(out_error);
            return false;
        }

        targ.files.emplace_back(nullptr, full_path_obj.string(), tar_path);

        auto& entry = targ.files.back();

        auto fd = new LibFlute::Transmitter::FileDescription(
            entry.kept_path.c_str(), 
            entry.kept_name.c_str()
        );

        if (fd->data() == nullptr) {
            delete fd;
            targ.files.pop_back(); 
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

        entry.file.reset(fd);
        return true;

    } catch(const std::exception &e) {
        out_error = e.what(); 
        spdlog::warn("Error creating fileEntry: {}", e.what());
        return false;
    }
}

json Broadcaster::send(const fs::path& tar_path) 
{
    std::vector<std::string> file_list;
    UpdateBuilder builder;
    json response;

    struct ScopeGuard {
        std::function<void()> f;
        ~ScopeGuard() { f(); }
    } cleanup_guard{[&]() {
        if (fs::exists(tar_path)) {
            std::error_code ec;
            std::filesystem::remove(tar_path.string(), ec);
        }
        
        for (const std::string& f : file_list) {
            if (std::filesystem::exists(f)) {
                std::error_code ec;
                std::filesystem::remove(f, ec);
            }
        }
    }};

    try {

        if (!this->target.transmitter) 
            throw std::runtime_error("Target has no transmitter");
        
        std::string error_msg;
        if (!this->create_file_entry(target, tar_path.string(), error_msg)) 
            throw std::runtime_error("Failed to add file " + tar_path.filename().string() + ": " + error_msg);        
        if (target.files.empty()) 
            throw std::runtime_error("No files where found for transmission");

        this->target.pending_files = this->target.files.size();
        LibFlute::Transmitter* transmitter = this->target.transmitter.get();

        for (const auto& file : this->target.files) {

            if (!file.file) {
                spdlog::error("File pointer is null!"); 
                continue; 
            }
            
            try {
                const auto &fe = file.file->file_entry();
                spdlog::info("Sending TOI: {}, URI: {}, Type: {}", 
                    file.file->toi(), 
                    fe.content_location, 
                    fe.content_type      
                );
            } catch (const std::exception& e) {
                spdlog::error("Crash while reading file properties: {}", e.what());
                continue;
            }

            auto toi = transmitter->send( file.file );
            if(toi == -1)
                throw std::runtime_error("sending to destination failed");
            
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Broadcasting update failed for package {}. Error: {}", 
                      tar_path.filename().string(), e.what());
        
        response = {
            {"status", "error"}, 
            {"message", std::string("Broadcasting failed: ") + e.what()}
        };
    }

    return response;
}

Broadcaster::Broadcaster() {
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    
    this->mcast_port = std::stoi(get_env_var("MCAST_PORT"));
    spdlog::info("MCAST_PORT successfully set to: {}", this->mcast_port);

    this->update_path = get_env_var("UPDATE_FILE_PATH");
    spdlog::info("UPDATE_FILE_PATH successfully set to: {}", this->update_path);

    this->assign_target(get_env_var("MCAST_IP"));
    spdlog::info("finished setting up the target, starting FLUTE thread");

    this->send_thread = std::thread([this] {
        spdlog::info("FLUTE io_context thread started");
        this->io.run();
    });
}

Broadcaster::~Broadcaster()
{
    this->io.stop();
    if (send_thread.joinable())
        send_thread.join();
}