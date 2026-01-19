#pragma once
#include <cstdint>
#include <Receiver.h>

struct ft_arguments {
  const char *flute_interface = {};  /**< file path of the config file. */
  const char *mcast_target = {};
  bool enable_ipsec = false;
  const char *aes_key = {};
  unsigned short mcast_port = 40085;
  unsigned log_level = 2;        /**< log level */
  char **files;
  uint64_t tsi = 16;
  const char *output_path = nullptr;
};


class FluteReceiver{

    void upload_update(const std::string& file_name); // post call to http server

    void process_file(const std::string& file_name); // call TopologicalSorted on file

    void set_receiver();

    boost::asio::io_context io;

    ft_arguments args;

    LibFlute::Receiver receiver;
public:
    FluteReceiver();

    ~FluteReceiver();

    void run();
}