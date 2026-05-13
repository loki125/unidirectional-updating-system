#pragma once

#include <cstdint>
#include <Receiver.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <memory>
#include <string>
#include <nlohmann/json.hpp> 

using json = nlohmann::json;

struct ft_arguments {
  std::string flute_interface = {};
  std::string mcast_target = {};
  bool enable_ipsec = false;
  unsigned short mcast_port;
  uint64_t tsi = 16;
  std::string output_path;
};


class FluteReceiver{

    void set_receiver();

    static void setup_multicast_route(const std::string& ip);

    boost::asio::io_context io;

    ft_arguments args;

    std::unique_ptr<LibFlute::Receiver> receiver;
public:
    FluteReceiver();

    ~FluteReceiver();

    void run();
};