#pragma once

#include <cstdint>
#include <Receiver.h>
#include <httplib.h>
#include <nlohmann/json.hpp> 

#include "TopologicalSorter.hpp"

using json = nlohmann::json;

struct ft_arguments {
  std::string flute_interface = {};  /**< file path of the config file. */
  std::string mcast_target = {};
  bool enable_ipsec = false;
  const char *aes_key = {};
  unsigned short mcast_port;
  char **files;
  uint64_t tsi = 16;
  std::string output_path;
};


class FluteReceiver{

    void upload_update(const std::string& file_name, const std::string &sort_json); // post call to http server

    void set_receiver();

    boost::asio::io_context io;

    std::string distributor_url;

    ft_arguments args;
    
    httplib::Client cli

    LibFlute::Receiver receiver;
public:
    FluteReceiver();

    ~FluteReceiver();

    void run();
}