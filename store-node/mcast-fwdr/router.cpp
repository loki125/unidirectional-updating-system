#include <iostream>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

const char* MCAST_GRP = "238.1.1.95";
const int MCAST_PORT = 40085;
const int BUFFER_SIZE = 65535;

// Network Interface Config
const char* IFACE_IN_NAME = "eth1";  // Sender Network
const char* IFACE_OUT_NAME = "eth0"; // Receiver Network

// Helper to get IP address of a network interface
struct in_addr get_interface_ip(const char* iface_name) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct ifreq ifr;
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, iface_name, IFNAMSIZ-1);

    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        std::cerr << "Error: Cannot find IP for interface " << iface_name 
                  << ". Make sure the network is connected." << std::endl;
        close(fd);
        exit(1);
    }
    close(fd);
    return ((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr;
}

int main() {
    std::cout << "Starting Multicast Router..." << std::endl;

    // Get Interface IPs
    struct in_addr ip_in = get_interface_ip(IFACE_IN_NAME);
    struct in_addr ip_out = get_interface_ip(IFACE_OUT_NAME);

    int sock_recv = socket(AF_INET, SOCK_DGRAM, 0);
    int reuse = 1;
    if (setsockopt(sock_recv, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse)) < 0) {
        perror("Setting SO_REUSEADDR error");
        return 1;
    }

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(MCAST_PORT);
    local_addr.sin_addr.s_addr = INADDR_ANY; 

    if (bind(sock_recv, (struct sockaddr*)&local_addr, sizeof(local_addr))) {
        perror("Binding datagram socket error");
        return 1;
    }

    // Join Multicast Group ONLY on Input Interface
    struct ip_mreq group;
    group.imr_multiaddr.s_addr = inet_addr(MCAST_GRP);
    group.imr_interface = ip_in; // Strict binding to eth0

    if (setsockopt(sock_recv, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&group, sizeof(group)) < 0) {
        perror("Adding multicast group error");
        return 1;
    }

    // Setup Sender Socket (Output)
    int sock_send = socket(AF_INET, SOCK_DGRAM, 0);
    
    // Set TTL
    unsigned char ttl = 2;
    setsockopt(sock_send, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    // Bind Outgoing Traffic ONLY to Output Interface
    if (setsockopt(sock_send, IPPROTO_IP, IP_MULTICAST_IF, (char*)&ip_out, sizeof(ip_out)) < 0) {
        perror("Setting output interface error");
        return 1;
    }

    // Destination Address
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr(MCAST_GRP);
    dest_addr.sin_port = htons(MCAST_PORT);

    std::cout << "Router Online. Forwarding " << MCAST_GRP << ":" << MCAST_PORT << std::endl;

    // Forwarding Loop
    std::vector<char> buffer(BUFFER_SIZE);

    while (true) {
        struct sockaddr_in src_addr;
        socklen_t addr_len = sizeof(src_addr);
        
        ssize_t len = recvfrom(sock_recv, buffer.data(), BUFFER_SIZE, 0, (struct sockaddr*)&src_addr, &addr_len);
        
        if (len > 0) {
            sendto(sock_send, buffer.data(), len, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        }

        std::cout << "Forwarded packet from " << inet_ntoa(src_addr.sin_addr) << ":" << ntohs(src_addr.sin_port) 
                  << " to " << MCAST_GRP << ":" << MCAST_PORT << std::endl;
    }

    return 0;
}