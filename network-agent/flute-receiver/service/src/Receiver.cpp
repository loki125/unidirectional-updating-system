#include "Receiver.hpp"


const char* set_env_var(const std::string& name){
    const char* var = std::getenv(name.data());
    if (!var) {
        throw std::runtime_error("Environment variable " + name + " is not set.");
    }
    return var;
}

void FluteReceiver::upload_update(const std::string& file_name, const std::string &sort_json)
{

    json payload;
    payload["update_file"] = file_name;
    payload["topological_sort"] = sort_json;

    try {

        auto res = this->cli.Post(this->distributor_path, body, "application/json");

        if (res) {
            if (res->status == 200 || res->status == 201) {
                spdlog::info("Successfully uploaded update to distributor: {}", this->distributor_path);
            } else {
                spdlog::error("Distributor returned error status: {}", res->status);
            }
        } else {
            auto err = res.error();
            spdlog::error("Failed to connect to distributor ({}): {}", this->distributor_path, httplib::to_string(err));
        }
    } catch (const std::exception& e) {
        spdlog::error("Exception occurred during upload_update: {}", e.what());
    }
}

void FluteReceiver::set_receiver()
{
    // Create the receiver
    this->receiver(
        this->args.flute_interface,
        this->args.mcast_target,
        (short)this->args.mcast_port,
        this->args.tsi,
        this->io);

    // Configure IPSEC, if enabled
    if (this->args.enable_ipsec)
    {
      this->receiver.enable_ipsec(1, this->args.aes_key);
    }

    this->receiver.register_completion_callback(
      [this, output_path = this->args.output_path](std::shared_ptr<LibFlute::File> file) { //NOLINT
        std::string out_file = file->meta().content_location;
        if (output_path && std::strlen(output_path) > 0) {
          out_file = (std::filesystem::path(output_path) / std::filesystem::path(out_file).filename()).string();
        }

        spdlog::info("{} (TOI {}) has been received",
                     out_file, file->meta().toi);
        FILE *fd = fopen(out_file.c_str(), "wb");
        fwrite(file->buffer(), 1, file->length(), fd);
        fclose(fd);

        try{
            json j = Topologicalsorter::topo_sort(out_file);
            this->upload_update(j);

        }catch(const std::exception &e){
            spdlog::error(e.what());
        }
      });
}

FluteReceiver::FluteReceiver()
{
    ft_arguments& args = this->args;

    args->mcast_port = std::stoi(set_env_var("FLUTE_PORT"));
    spdlog::info("FLUTE_PORT successfully set to: {}", args->mcast_port);

    args->mcast_target = set_env_var("IP");
    spdlog::info("IP successfully set to: {}", args->mcast_target);

    args->output_path = set_env_var("PATH");
    spdlog::info("OUTPUT_PATH successfully set to: {}", args->output_path);

    std::string url = set_env_var("DISTRIBUTOR_URL");
    std::string method = set_env_var("UPDATE_FILE_REQUEST");
    spdlog::info("DISTRIBUTOR_URL, UPDATE_FILE_REQUEST successfully set to: {}, {}", url, method);

    this->distributor_path = url + method;
    this->cli(url);

    this->cli.set_connection_timeout(5);
    this->cli.set_read_timeout(5);

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
