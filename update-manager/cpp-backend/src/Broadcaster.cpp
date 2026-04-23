#include "Broadcaster.hpp"

void Broadcaster::assign_target(const std::string& ip){
    this->target = Target();
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

            Target* targ = &this->target
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

        targ.files.emplace_back(nullptr, full_path_obj.string(), file_path);

        auto& entry = targ.files.back();

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

        // The unique_ptr/shared_ptr takes ownership here
        entry.file.reset(fd);

        return true;

    } catch(const std::exception &e) {
        out_error = e.what(); 
        spdlog::warn("Error creating fileEntry: {}", e.what());
        return false;
    }
}

json Broadcaster::send(
    const std::string& main_package_path, 
    PackageMetadata& update_metadata, 
    PackageService* engine, 
    const std::string& broadcaster_path) 
{
    std::string file_path;
    std::vector<std::string> file_list;
    json response;

    // --- Clean Up Lambda ---
    struct ScopeGuard {
        std::function<void()> f;
        ~ScopeGuard() { f(); }
    } cleanup_guard{[&]() {
        // Clean up the actual generated tar file
        if (!file_path.empty() && std::filesystem::exists(file_path)) {
            std::error_code ec;
            std::filesystem::remove(file_path, ec);
        }
        
        for (const std::string& f : file_list) {
            if (f != main_package_path && std::filesystem::exists(f)) {
                std::error_code ec;
                std::filesystem::remove(f, ec);
            }
        }
    }};

    try {
        std::vector<PackageMetadata> packages_for_manifest;
        long long total_size_byte = 0;
        
        // 1. Initialize lists with the main package
        file_list.push_back(main_package_path);
        packages_for_manifest.push_back(update_metadata);
        total_size_byte += update_metadata.Size;

        // 2. Delegate dependency resolution to the engine
        // This single call dynamically resolves, checks constraints, queues, 
        // and downloads ALL recursive dependencies.
        std::vector<PackageMetadata> dependencies_metadata = engine->get_recursive_dependencies(
            update_metadata, 
            main_package_path
        );

        // 3. Add the resolved dependencies to our build lists
        for (const auto& dep_meta : dependencies_metadata) {
            std::string dep_file_path = dep_meta.Filename; 
            
            // If the file is not already in the list, add it
            if (std::find(file_list.begin(), file_list.end(), dep_file_path) == file_list.end()) {
                file_list.push_back(dep_file_path);
                packages_for_manifest.push_back(dep_meta);
                total_size_byte += dep_meta.Size;
            }
        }

        // 4. Construct the Manifest
        UpdateManifest manifest;
        manifest.update_id = update_metadata.generate_id();
        manifest.pkgs_type = update_metadata.Type;
        manifest.format_version = "1.0"; // Replace with self.version equivalent
        manifest.total_size_byte = total_size_byte;
        manifest.packages = packages_for_manifest;

        // Generate current timestamp (ISO 8601 format)
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&now_time), "%Y-%m-%dT%H:%M:%SZ");
        manifest.timestamp = ss.str();

        // 5. Finalize: Create the tarball
        file_path = this->create_tar_object(manifest, file_list, broadcaster_path);

                if (!this->target.transmitter) 
            throw std::runtime_error("Target has no transmitter");
        
        std::string error_msg;
        if (!this->create_file_entry(target, file_path, error_msg)) 
            throw std::runtime_error("Failed to add file {}: {}", file_path, error_msg);
        
        // Only attempt send if files were added successfully
        if (target.files.empty()) 
            throw std::runtime_error("No files where found for transmission");

        this->target.pending_files = this->target.files.size();
        LibFlute::Transmitter* transmitter = this->target.transmitter.get();

        // Queue all the files
        for (const auto& file : this->target.files) {

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
                throw std::runtime_error("sending to destination failed");
            
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Broadcasting update failed for package {}. Error: {}", 
                      update_metadata.generate_id(), e.what());
        
        response = {
            {"status", "error"}, 
            {"message", std::string("Broadcasting failed: ") + e.what()}
        };
    }

    // Call the cleanup lambda before returning
    cleanup();

    return response;
}


std::string Broadcaster::create_tar_object(const UpdateManifest& manifest, 
                                           const std::vector<std::string>& file_paths, 
                                           const std::string& tar_path) 
{
    // Define the file path (using std::filesystem)
    std::string new_file_path = (std::filesystem::path(tar_path) / (manifest.update_id + ".tar")).string();

    // Initialize libarchive writer
    struct archive *tar = archive_write_new();
    
    // Use the standard tar format (POSIX pax)
    archive_write_set_format_pax_restricted(tar); 
    if (archive_write_open_filename(tar, new_file_path.c_str()) != ARCHIVE_OK) {
        archive_write_free(tar);
        throw std::runtime_error("Failed to open tar file for writing: " + new_file_path);
    }

    // 1. Add the manifest JSON directly from memory!
    if (!manifest.packages.empty()) {
        std::string manifest_data = manifest.to_json();
        
        struct archive_entry *entry = archive_entry_new();
        archive_entry_set_pathname(entry, "manifest.json");
        archive_entry_set_size(entry, manifest_data.size());
        archive_entry_set_filetype(entry, AE_IFREG); // Regular file
        archive_entry_set_perm(entry, 0644);         // Standard permissions
        
        archive_write_header(tar, entry);
        archive_write_data(tar, manifest_data.c_str(), manifest_data.size());
        
        archive_entry_free(entry);
    }

    // 2. Add all physical component files
    for (const std::string& path : file_paths) {
        if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
            archive_write_close(tar);
            archive_write_free(tar);
            throw std::runtime_error("Missing component file: " + path);
        }

        struct archive_entry *file_entry = archive_entry_new();
        
        // Ensure we don't include the full local folder structure in the tar
        // ( equivalent to arcname=os.path.basename(path) in python )
        std::string base_name = std::filesystem::path(path).filename().string();
        archive_entry_set_pathname(file_entry, base_name.c_str());
        
        // Set metadata
        size_t file_size = std::filesystem::file_size(path);
        archive_entry_set_size(file_entry, file_size);
        archive_entry_set_filetype(file_entry, AE_IFREG);
        archive_entry_set_perm(file_entry, 0644);
        
        archive_write_header(tar, file_entry);

        // Read the file and stream it into the tar archive in chunks (Memory Efficient)
        std::ifstream ifs(path, std::ios::binary);
        char buff[8192]; // 8 KB Buffer
        while (ifs.read(buff, sizeof(buff))) {
            archive_write_data(tar, buff, ifs.gcount());
        }
        // Write the last partial chunk if any
        if (ifs.gcount() > 0) {
            archive_write_data(tar, buff, ifs.gcount());
        }

        archive_entry_free(file_entry);
    }

    // Finalize and close the archive
    archive_write_close(tar);
    archive_write_free(tar);

    return new_file_path;
}

Broadcaster::Broadcaster() {
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    
    // extract target from env, set defualt_args for each and insert it to target
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