#pragma once

#include <cstdint>
#include <Receiver.h>
#include <httplib.h>
#include <memory>
#include <string>
#include <nlohmann/json.hpp> 

#include "TopologicalSorter.hpp"

using json = nlohmann::json;

struct ft_arguments {
  std::string flute_interface = {};  /**< file path of the config file. */
  std::string mcast_target = {};
  bool enable_ipsec = false;
  const char *aes_key = {};
  unsigned short mcast_port;
  uint64_t tsi = 16;
  std::string output_path;
};


class FluteReceiver{

    void upload_update(const std::string& file_name, const json &sort_json); // post call to http server

    void set_receiver();

    boost::asio::io_context io;

    std::string distributor_path;

    ft_arguments args;

    std::unique_ptr<httplib::Client> cli;

    std::unique_ptr<LibFlute::Receiver> receiver;
public:
    FluteReceiver();

    ~FluteReceiver();

    void run();
};