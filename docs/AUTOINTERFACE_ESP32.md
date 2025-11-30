# ESP32 AutoInterface Implementation

This document describes the ESP32 implementation of Reticulum's AutoInterface for IPv6 multicast peer discovery.

## Overview

The AutoInterface provides automatic peer discovery over IPv6 link-local networks using multicast. Peers announce themselves by sending discovery tokens to a multicast group, and accept data from validated peers via unicast UDP.

## Protocol Details

- **Discovery Port**: 29716 (multicast)
- **Data Port**: 42671 (unicast)
- **Group ID**: "reticulum" (default)
- **Multicast Address**: Calculated from `full_hash(group_id)` - e.g., `ff12:eac4:d70b:fb1c:16e4:5e39:485e:31e1`
- **Discovery Token**: `full_hash(group_id + link_local_address)[:16]` (first 16 bytes)
- **Peer Timeout**: 10 seconds

## ESP32-Specific Implementation

### Key Differences from POSIX

The ESP32 Arduino framework has several limitations that required workarounds:

1. **WiFiUDP doesn't support IPv6**: Raw lwIP sockets are used for both discovery and data.

2. **IPAddress is IPv4-only**: The `IPAddress` class only stores 4 bytes. Must use `IPv6Address` (16 bytes) for peer addresses.

3. **setsockopt IPV6_JOIN_GROUP not supported**: Use `mld6_joingroup_netif()` directly from lwIP.

4. **Interface scope_id**: Link-local addresses require the correct interface index. Use `netif_get_index()` to get it.

### Socket Setup

```cpp
// Discovery socket (multicast receive + send)
_discovery_socket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
// Bind to in6addr_any on discovery port
// Join multicast via mld6_joingroup_netif()

// Data socket (unicast send + receive)
_data_socket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
// Bind to link-local address on data port
// Use netif_get_index() for scope_id
```

### IPv6 Address Handling

```cpp
// WRONG - IPAddress only reads 4 bytes!
IPAddress addr((const uint8_t*)&sockaddr.sin6_addr);

// CORRECT - IPv6Address reads all 16 bytes
IPv6Address addr((const uint8_t*)&sockaddr.sin6_addr);
```

### Sending to Link-Local Addresses

For link-local addresses, the `sin6_scope_id` must be set correctly:

```cpp
struct sockaddr_in6 peer_addr;
peer_addr.sin6_family = AF_INET6;
peer_addr.sin6_port = htons(port);
peer_addr.sin6_scope_id = _if_index;  // From netif_get_index()
memcpy(&peer_addr.sin6_addr, addr_bytes, 16);

sendto(_data_socket, data, len, 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
```

## Working Features

- [x] Discovery token calculation (matches Python RNS exactly)
- [x] Multicast group join via MLD6
- [x] Multicast discovery announce sending
- [x] Discovery token reception and validation
- [x] Peer tracking with IPv6 addresses
- [x] Unicast data packet sending via raw sockets
- [x] Data packets reach network (verified with tcpdump)
- [x] Peer expiration after timeout

## Verified Interoperability

**ESP32 -> Python RNS:**
- ESP32 receives Python's discovery tokens via unicast
- ESP32 validates tokens and adds Python as peer
- ESP32 sends data packets to Python (167-byte announce packets confirmed)

**Python RNS -> ESP32:**
- Python sends discovery tokens to ESP32 (working)
- ESP32 receives and processes them correctly

## Fixed Issues (2025-11-29)

### Multicast Discovery Now Working

Previous issue: Python RNS was not receiving ESP32's multicast discovery announces.

**Root cause**: The ESP32 code was missing the interface specification for multicast sends:
1. Missing `setsockopt(IPV6_MULTICAST_IF)` on discovery socket
2. Missing `sin6_scope_id` in destination address for `sendto()`

Without these, the kernel doesn't know which interface to use for multicast packets.

**Fix applied**:
- Added `IPV6_MULTICAST_IF` setsockopt in `setup_discovery_socket()`
- Set `dest_addr.sin6_scope_id = _if_index` in `send_announce()`
- Get `_if_index` earlier (in discovery socket setup, not just data socket)

This matches Python RNS behavior which sets `IPV6_MULTICAST_IF` (lines 254, 444 in AutoInterface.py).

## Files Modified

- `examples/common/auto_interface/AutoInterface.h` - Added IPv6Address, _data_socket, _if_index
- `examples/common/auto_interface/AutoInterface.cpp` - Raw socket implementation
- `examples/common/auto_interface/AutoInterfacePeer.h` - Changed IPAddress to IPv6Address

## Testing

1. Build and flash the transport_node_tbeam_supreme example
2. Monitor serial output for discovery activity
3. Send discovery tokens from Python to establish peer
4. Press Enter on serial to trigger announce
5. Use tcpdump to verify packets: `sudo tcpdump -i wlan0 'ip6 src ESP32_ADDR' -n`

## References

- [Reticulum AutoInterface](https://github.com/markqvist/Reticulum/blob/master/RNS/Interfaces/AutoInterface.py)
- [ESP32 lwIP IPv6](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/lwip.html)
- [RFC 4291 - IPv6 Addressing](https://tools.ietf.org/html/rfc4291)
