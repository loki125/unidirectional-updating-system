#pragma once

#include <cstdint>
#include <Receiver.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <memory>
#include <string>
#include <nlohmann/json.hpp> 

using json = nlohmann::json;

/**
 * @brief Configuration parameters for the FLUTE receiver session.
 */
struct ft_arguments {
  std::string flute_interface = {}; ///< Network interface to bind for FLUTE traffic.
  std::string mcast_target = {};    ///< Destination multicast IP address.
  bool enable_ipsec = false;        ///< Flag to enable/disable IPsec security.
  unsigned short mcast_port;        ///< Multicast port number.
  uint64_t tsi = 16;                ///< Transport Session Identifier.
  std::string output_path;          ///< Base directory for saving received files.
};

/**
 * @brief Manages the reception of files over multicast using the FLUTE protocol.
 */
class FluteReceiver {

    /**
     * @brief Initializes the LibFlute receiver and defines the file assembly callback.
     */
    void set_receiver();

    /**
     * @brief Configures system-level routing to allow multicast traffic on the appropriate interface.
     * @param ip The IP address used to identify the target network interface.
     */
    static void setup_multicast_route(const std::string& ip);

    boost::asio::io_context io; ///< IO context for managing asynchronous network events.

    ft_arguments args; ///< Internal configuration state loaded from environment.

    std::unique_ptr<LibFlute::Receiver> receiver; ///< The underlying LibFlute receiver instance.

public:
    /**
     * @brief Constructs the receiver, sets up routing, and loads environment configuration.
     */
    FluteReceiver();

    /**
     * @brief Ensures the IO context is stopped during destruction.
     */
    ~FluteReceiver();

    /**
     * @brief Starts the asynchronous IO loop to begin receiving data.
     */
    void run();
};