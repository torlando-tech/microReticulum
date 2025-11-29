#include "AutoInterface.h"
#include "../src/Log.h"
#include "../src/Utilities/OS.h"

#include <cstring>
#include <algorithm>

#ifndef ARDUINO
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <net/if.h>
#include <ifaddrs.h>
#endif

using namespace RNS;

AutoInterface::AutoInterface(const char* name) : InterfaceImpl(name) {
    _IN = true;
    _OUT = true;
    _bitrate = BITRATE_GUESS;
    _HW_MTU = HW_MTU;
    memset(&_multicast_address, 0, sizeof(_multicast_address));
    memset(&_link_local_address, 0, sizeof(_link_local_address));
}

AutoInterface::~AutoInterface() {
    stop();
}

bool AutoInterface::start() {
    _online = false;

    INFO("AutoInterface: Starting with group_id: " + _group_id);
    INFO("AutoInterface: Discovery port: " + std::to_string(_discovery_port));
    INFO("AutoInterface: Data port: " + std::to_string(_data_port));

#ifdef ARDUINO
    // ESP32 implementation - TODO in Phase 3
    ERROR("AutoInterface: ESP32 support not yet implemented");
    return false;
#else
    // Get link-local address for our interface
    if (!get_link_local_address()) {
        ERROR("AutoInterface: Could not get link-local IPv6 address");
        return false;
    }

    // Calculate multicast address from group_id hash
    calculate_multicast_address();

    // Calculate our discovery token
    calculate_discovery_token();

    // Set up discovery socket (multicast receive)
    if (!setup_discovery_socket()) {
        ERROR("AutoInterface: Could not set up discovery socket");
        return false;
    }

    // Set up data socket (unicast send/receive)
    if (!setup_data_socket()) {
        // Data socket failure is non-fatal - we can still discover peers
        // This happens when Python RNS is already bound to the same address:port
        WARNING("AutoInterface: Could not set up data socket (discovery-only mode)");
        WARNING("AutoInterface: Another RNS instance may be using this address");
    }

    _online = true;
    INFO("AutoInterface: Started successfully (data_socket=" +
         std::string(_data_socket >= 0 ? "yes" : "no") + ")");
    INFO("AutoInterface: Multicast address: " + std::string(inet_ntop(AF_INET6, &_multicast_address,
        (char*)_buffer.writable(INET6_ADDRSTRLEN), INET6_ADDRSTRLEN)));
    INFO("AutoInterface: Link-local address: " + _link_local_address_str);
    INFO("AutoInterface: Discovery token: " + _discovery_token.toHex());

    return true;
#endif
}

void AutoInterface::stop() {
#ifdef ARDUINO
    // ESP32 cleanup
#else
    if (_discovery_socket > -1) {
        close(_discovery_socket);
        _discovery_socket = -1;
    }
    if (_data_socket > -1) {
        close(_data_socket);
        _data_socket = -1;
    }
#endif
    _online = false;
    _peers.clear();
}

void AutoInterface::loop() {
    if (!_online) return;

#ifndef ARDUINO
    double now = RNS::Utilities::OS::time();

    // Send periodic discovery announce
    if (now - _last_announce >= ANNOUNCE_INTERVAL) {
        send_announce();
        _last_announce = now;
    }

    // Process incoming discovery packets
    process_discovery();

    // Process incoming data packets
    process_data();

    // Expire stale peers
    expire_stale_peers();

    // Expire old deque entries
    expire_deque_entries();
#endif
}

void AutoInterface::send_outgoing(const Bytes& data) {
    DEBUG(toString() + ".send_outgoing: data: " + data.toHex());

    if (!_online) return;

#ifndef ARDUINO
    // Send to all known peers via unicast
    for (const auto& peer : _peers) {
        if (peer.is_local) continue;  // Don't send to ourselves

        struct sockaddr_in6 peer_addr;
        memset(&peer_addr, 0, sizeof(peer_addr));
        peer_addr.sin6_family = AF_INET6;
        peer_addr.sin6_port = htons(_data_port);
        peer_addr.sin6_addr = peer.address;
        peer_addr.sin6_scope_id = _if_index;

        ssize_t sent = sendto(_data_socket, data.data(), data.size(), 0,
                              (struct sockaddr*)&peer_addr, sizeof(peer_addr));
        if (sent < 0) {
            WARNING("AutoInterface: Failed to send to peer " + peer.address_string() +
                    ": " + std::string(strerror(errno)));
        } else {
            TRACE("AutoInterface: Sent " + std::to_string(sent) + " bytes to " + peer.address_string());
        }
    }

    // Perform post-send housekeeping
    InterfaceImpl::handle_outgoing(data);
#endif
}

#ifndef ARDUINO

bool AutoInterface::get_link_local_address() {
    struct ifaddrs* ifaddr;
    if (getifaddrs(&ifaddr) == -1) {
        ERROR("AutoInterface: getifaddrs failed: " + std::string(strerror(errno)));
        return false;
    }

    bool found = false;
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET6) continue;

        // Skip loopback
        if (strcmp(ifa->ifa_name, "lo") == 0) continue;

        // If interface name specified, match it
        if (!_ifname.empty() && _ifname != ifa->ifa_name) continue;

        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)ifa->ifa_addr;

        // Check for link-local address (fe80::/10)
        if (IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr)) {
            _link_local_address = addr6->sin6_addr;
            _if_index = if_nametoindex(ifa->ifa_name);
            _ifname = ifa->ifa_name;

            // Convert to string for token generation
            char buf[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &addr6->sin6_addr, buf, sizeof(buf));
            _link_local_address_str = buf;

            INFO("AutoInterface: Found link-local address " + _link_local_address_str +
                 " on interface " + _ifname);
            found = true;
            break;
        }
    }

    freeifaddrs(ifaddr);
    return found;
}

void AutoInterface::calculate_multicast_address() {
    // Python: group_hash = RNS.Identity.full_hash(self.group_id)
    Bytes group_id_bytes((const uint8_t*)_group_id.c_str(), _group_id.length());
    Bytes group_hash = Identity::full_hash(group_id_bytes);

    // Build multicast address: ff12:0:XXXX:XXXX:XXXX:XXXX:XXXX:XXXX
    // ff = multicast prefix
    // 1 = temporary address type (MULTICAST_TEMPORARY_ADDRESS_TYPE)
    // 2 = link scope (SCOPE_LINK)
    // The remaining 112 bits come from the group hash

    // Python format: "ff12:0:" + formatted hash segments
    // Each segment is: g[n+1] + (g[n] << 8) for pairs starting at index 2

    uint8_t addr[16];
    addr[0] = 0xff;
    addr[1] = 0x12;  // 1=temporary, 2=link scope

    // First 16-bit group is 0
    addr[2] = 0x00;
    addr[3] = 0x00;

    // Python: gt += ':' + '{:02x}'.format(g[3]+(g[2]<<8))
    // This creates little-endian 16-bit values from hash bytes
    // But for IPv6 address, we need network byte order (big-endian)

    const uint8_t* g = group_hash.data();

    // Segment 2: g[3]+(g[2]<<8) = little-endian, then stored big-endian
    addr[4] = g[2];
    addr[5] = g[3];

    // Segment 3: g[5]+(g[4]<<8)
    addr[6] = g[4];
    addr[7] = g[5];

    // Segment 4: g[7]+(g[6]<<8)
    addr[8] = g[6];
    addr[9] = g[7];

    // Segment 5: g[9]+(g[8]<<8)
    addr[10] = g[8];
    addr[11] = g[9];

    // Segment 6: g[11]+(g[10]<<8)
    addr[12] = g[10];
    addr[13] = g[11];

    // Segment 7: g[13]+(g[12]<<8)
    addr[14] = g[12];
    addr[15] = g[13];

    memcpy(&_multicast_address, addr, 16);
    _multicast_address_bytes = Bytes(addr, 16);
}

void AutoInterface::calculate_discovery_token() {
    // Python: discovery_token = RNS.Identity.full_hash(self.group_id+link_local_address.encode("utf-8"))
    Bytes combined;
    combined.append((const uint8_t*)_group_id.c_str(), _group_id.length());
    combined.append((const uint8_t*)_link_local_address_str.c_str(), _link_local_address_str.length());

    _discovery_token = Identity::full_hash(combined);
    TRACE("AutoInterface: Discovery token input: " + combined.toHex());
    TRACE("AutoInterface: Discovery token: " + _discovery_token.toHex());
}

bool AutoInterface::setup_discovery_socket() {
    // Create IPv6 UDP socket
    _discovery_socket = socket(AF_INET6, SOCK_DGRAM, 0);
    if (_discovery_socket < 0) {
        ERROR("AutoInterface: Could not create discovery socket: " + std::string(strerror(errno)));
        return false;
    }

    // Enable address reuse
    int reuse = 1;
    setsockopt(_discovery_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(_discovery_socket, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    // Set multicast interface
    setsockopt(_discovery_socket, IPPROTO_IPV6, IPV6_MULTICAST_IF, &_if_index, sizeof(_if_index));

    // Join multicast group
    if (!join_multicast_group()) {
        close(_discovery_socket);
        _discovery_socket = -1;
        return false;
    }

    // Bind to discovery port on multicast address
    struct sockaddr_in6 bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin6_family = AF_INET6;
    bind_addr.sin6_port = htons(_discovery_port);
    bind_addr.sin6_addr = _multicast_address;
    bind_addr.sin6_scope_id = _if_index;

    if (bind(_discovery_socket, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        ERROR("AutoInterface: Could not bind discovery socket: " + std::string(strerror(errno)));
        close(_discovery_socket);
        _discovery_socket = -1;
        return false;
    }

    // Make socket non-blocking
    int flags = 1;
    ioctl(_discovery_socket, FIONBIO, &flags);

    INFO("AutoInterface: Discovery socket bound to port " + std::to_string(_discovery_port));
    return true;
}

bool AutoInterface::setup_data_socket() {
    // Create IPv6 UDP socket for data
    _data_socket = socket(AF_INET6, SOCK_DGRAM, 0);
    if (_data_socket < 0) {
        ERROR("AutoInterface: Could not create data socket: " + std::string(strerror(errno)));
        return false;
    }

    // Enable address reuse
    int reuse = 1;
    setsockopt(_data_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(_data_socket, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    // Bind to data port on link-local address
    struct sockaddr_in6 bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin6_family = AF_INET6;
    bind_addr.sin6_port = htons(_data_port);
    bind_addr.sin6_addr = _link_local_address;
    bind_addr.sin6_scope_id = _if_index;

    if (bind(_data_socket, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        ERROR("AutoInterface: Could not bind data socket: " + std::string(strerror(errno)));
        close(_data_socket);
        _data_socket = -1;
        return false;
    }

    // Make socket non-blocking
    int flags = 1;
    ioctl(_data_socket, FIONBIO, &flags);

    INFO("AutoInterface: Data socket bound to port " + std::to_string(_data_port));
    return true;
}

bool AutoInterface::join_multicast_group() {
    struct ipv6_mreq mreq;
    memcpy(&mreq.ipv6mr_multiaddr, &_multicast_address, sizeof(_multicast_address));
    mreq.ipv6mr_interface = _if_index;

    if (setsockopt(_discovery_socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq)) < 0) {
        ERROR("AutoInterface: Could not join multicast group: " + std::string(strerror(errno)));
        return false;
    }

    char buf[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &_multicast_address, buf, sizeof(buf));
    INFO("AutoInterface: Joined multicast group " + std::string(buf));
    return true;
}

void AutoInterface::send_announce() {
    if (_discovery_socket < 0) return;

    // Send discovery token to multicast address
    struct sockaddr_in6 mcast_addr;
    memset(&mcast_addr, 0, sizeof(mcast_addr));
    mcast_addr.sin6_family = AF_INET6;
    mcast_addr.sin6_port = htons(_discovery_port);
    mcast_addr.sin6_addr = _multicast_address;
    mcast_addr.sin6_scope_id = _if_index;

    ssize_t sent = sendto(_discovery_socket, _discovery_token.data(), _discovery_token.size(), 0,
                          (struct sockaddr*)&mcast_addr, sizeof(mcast_addr));
    if (sent < 0) {
        WARNING("AutoInterface: Failed to send discovery announce: " + std::string(strerror(errno)));
    } else {
        TRACE("AutoInterface: Sent discovery announce (" + std::to_string(sent) + " bytes)");
    }
}

void AutoInterface::process_discovery() {
    if (_discovery_socket < 0) return;

    uint8_t recv_buffer[1024];
    struct sockaddr_in6 src_addr;
    socklen_t addr_len = sizeof(src_addr);

    while (true) {
        ssize_t len = recvfrom(_discovery_socket, recv_buffer, sizeof(recv_buffer), 0,
                               (struct sockaddr*)&src_addr, &addr_len);
        if (len <= 0) break;

        // Get source address string
        char src_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &src_addr.sin6_addr, src_str, sizeof(src_str));

        DEBUG("AutoInterface: Received discovery packet from " + std::string(src_str) +
              " (" + std::to_string(len) + " bytes)");

        // Verify the peering hash
        // Python: expected_hash = RNS.Identity.full_hash(self.group_id+ipv6_src[0].encode("utf-8"))
        Bytes combined;
        combined.append((const uint8_t*)_group_id.c_str(), _group_id.length());
        combined.append((const uint8_t*)src_str, strlen(src_str));
        Bytes expected_hash = Identity::full_hash(combined);

        // Compare received hash with expected
        if (len >= 32 && memcmp(recv_buffer, expected_hash.data(), 32) == 0) {
            // Valid peer
            add_or_refresh_peer(src_addr.sin6_addr, RNS::Utilities::OS::time());
        } else {
            DEBUG("AutoInterface: Invalid discovery hash from " + std::string(src_str));
        }
    }
}

void AutoInterface::process_data() {
    if (_data_socket < 0) return;

    struct sockaddr_in6 src_addr;
    socklen_t addr_len = sizeof(src_addr);

    while (true) {
        _buffer.clear();
        ssize_t len = recvfrom(_data_socket, _buffer.writable(Type::Reticulum::MTU),
                               Type::Reticulum::MTU, 0,
                               (struct sockaddr*)&src_addr, &addr_len);
        if (len <= 0) break;

        _buffer.resize(len);

        // Check for duplicates (multi-interface deduplication)
        if (is_duplicate(_buffer)) {
            TRACE("AutoInterface: Dropping duplicate packet");
            continue;
        }

        add_to_deque(_buffer);

        char src_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &src_addr.sin6_addr, src_str, sizeof(src_str));
        DEBUG("AutoInterface: Received data from " + std::string(src_str) +
              " (" + std::to_string(len) + " bytes)");

        // Pass to transport
        InterfaceImpl::handle_incoming(_buffer);
    }
}

void AutoInterface::add_or_refresh_peer(const struct in6_addr& addr, double timestamp) {
    // Check if this is our own address
    if (memcmp(&addr, &_link_local_address, sizeof(addr)) == 0) {
        DEBUG("AutoInterface: Received own multicast echo - ignoring");
        return;
    }

    // Check if peer already exists
    for (auto& peer : _peers) {
        if (peer.same_address(addr)) {
            peer.last_heard = timestamp;
            TRACE("AutoInterface: Refreshed peer " + peer.address_string());
            return;
        }
    }

    // Add new peer
    AutoInterfacePeer new_peer(addr, _data_port, timestamp);
    _peers.push_back(new_peer);

    INFO("AutoInterface: Added new peer " + new_peer.address_string());
}

void AutoInterface::expire_stale_peers() {
    double now = RNS::Utilities::OS::time();

    _peers.erase(
        std::remove_if(_peers.begin(), _peers.end(),
            [this, now](const AutoInterfacePeer& peer) {
                if (now - peer.last_heard > PEERING_TIMEOUT) {
                    INFO("AutoInterface: Removed stale peer " + peer.address_string());
                    return true;
                }
                return false;
            }),
        _peers.end());
}

bool AutoInterface::is_duplicate(const Bytes& packet) {
    Bytes packet_hash = Identity::full_hash(packet);

    for (const auto& entry : _packet_deque) {
        if (entry.hash == packet_hash) {
            return true;
        }
    }
    return false;
}

void AutoInterface::add_to_deque(const Bytes& packet) {
    DequeEntry entry;
    entry.hash = Identity::full_hash(packet);
    entry.timestamp = RNS::Utilities::OS::time();

    _packet_deque.push_back(entry);

    // Limit deque size
    while (_packet_deque.size() > DEQUE_SIZE) {
        _packet_deque.pop_front();
    }
}

void AutoInterface::expire_deque_entries() {
    double now = RNS::Utilities::OS::time();

    while (!_packet_deque.empty() && now - _packet_deque.front().timestamp > DEQUE_TTL) {
        _packet_deque.pop_front();
    }
}

#endif // !ARDUINO
