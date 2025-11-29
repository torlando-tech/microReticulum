#pragma once

#include <string>
#include <cstdint>

#ifdef ARDUINO
#include <WiFi.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

// Lightweight peer info holder for AutoInterface
// Not a full InterfaceImpl - just holds address and timing info
struct AutoInterfacePeer {
    // IPv6 address storage
#ifdef ARDUINO
    IPAddress address;
#else
    struct in6_addr address;
#endif
    uint16_t data_port;
    double last_heard;      // Timestamp of last activity
    bool is_local;          // True if this is our own announcement (to ignore)

    AutoInterfacePeer() : data_port(0), last_heard(0), is_local(false) {
#ifndef ARDUINO
        memset(&address, 0, sizeof(address));
#endif
    }

#ifdef ARDUINO
    AutoInterfacePeer(const IPAddress& addr, uint16_t port, double time, bool local = false)
        : address(addr), data_port(port), last_heard(time), is_local(local) {}
#else
    AutoInterfacePeer(const struct in6_addr& addr, uint16_t port, double time, bool local = false)
        : address(addr), data_port(port), last_heard(time), is_local(local) {}
#endif

    // Get string representation of address for logging
    std::string address_string() const {
#ifdef ARDUINO
        return address.toString().c_str();
#else
        char buf[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &address, buf, sizeof(buf));
        return std::string(buf);
#endif
    }

    // Check if two peers have the same address
#ifndef ARDUINO
    bool same_address(const struct in6_addr& other) const {
        return memcmp(&address, &other, sizeof(address)) == 0;
    }
#endif
};
