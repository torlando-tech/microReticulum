# Phase 3 Research: Network Handling

## Objective

Implement network change handling to gracefully recover when the IPv6 link-local address changes (e.g., after WiFi reconnection or network interface reset).

## Python RNS Reference Analysis

### Link-Local Address Change Detection (AutoInterface.py:407-422)

In the peer job loop, Python RNS checks if the link-local address changed:

```python
for ifname in self.adopted_interfaces:
    try:
        addresses = self.list_addresses(ifname)
        if self.netinfo.AF_INET6 in addresses:
            link_local_addr = None
            for address in addresses[self.netinfo.AF_INET6]:
                if "addr" in address:
                    if address["addr"].startswith("fe80:"):
                        link_local_addr = self.descope_linklocal(address["addr"])
                        if link_local_addr != self.adopted_interfaces[ifname]:
                            # Address changed!
                            old_link_local_address = self.adopted_interfaces[ifname]
                            RNS.log("Replacing link-local address "+str(old_link_local_address)+" for "+str(ifname)+" with "+str(link_local_addr), RNS.LOG_DEBUG)
                            self.adopted_interfaces[ifname] = link_local_addr
                            self.link_local_addresses.append(link_local_addr)

                            if old_link_local_address in self.link_local_addresses:
                                self.link_local_addresses.remove(old_link_local_address)
```

### Socket Rebinding on Address Change (AutoInterface.py:424-443)

After detecting address change, Python RNS:
1. Closes existing data socket listener
2. Creates new socket bound to new address
3. Updates discovery token
4. Sets `carrier_changed` flag

```python
local_addr = link_local_addr+"%"+ifname
addr_info = socket.getaddrinfo(local_addr, self.data_port, socket.AF_INET6, socket.SOCK_DGRAM)
listen_address = addr_info[0][4]

# Close old socket and rebind
# ... spawn new listener thread with new address
self.carrier_changed = True
```

### Timeout State Tracking (AutoInterface.py:132, 456-464)

Python RNS tracks timeout state per-interface in `timed_out_interfaces` dict:

```python
self.timed_out_interfaces = {}  # ifname -> bool

# In peer job loop:
if now - last_multicast_echo > self.multicast_echo_timeout:
    if ifname in self.timed_out_interfaces and self.timed_out_interfaces[ifname] == False:
        self.carrier_changed = True
        RNS.log("Multicast echo timeout for "+str(ifname)+". Carrier lost.", RNS.LOG_WARNING)
    self.timed_out_interfaces[ifname] = True
else:
    if ifname in self.timed_out_interfaces and self.timed_out_interfaces[ifname] == True:
        self.carrier_changed = True
        RNS.log(str(self)+" Carrier recovered on "+str(ifname), RNS.LOG_WARNING)
    self.timed_out_interfaces[ifname] = False
```

## C++ Implementation Plan

### Current State

The C++ AutoInterface already has:
- `_link_local_address` (in6_addr) and `_link_local_address_str` (string)
- `_link_local_ip` (IPv6Address for ESP32)
- `get_link_local_address()` method that finds the link-local address at startup
- `_timed_out` flag (added in Phase 2)
- `_carrier_changed` flag (added in Phase 2)

### New Features Needed

1. **Periodic address check** - Call `check_link_local_address()` every PEER_JOB_INTERVAL (4s)
2. **Address change detection** - Compare current address to stored address
3. **Socket rebinding** - Close and recreate data socket on change
4. **Token recalculation** - Update discovery token for new address

### New Constants (AutoInterface.h)

```cpp
static constexpr double PEER_JOB_INTERVAL = 4.0;  // seconds (matches Python RNS)
```

### New Member Variables (AutoInterface.h)

```cpp
double _last_peer_job = 0;  // Timestamp of last peer job check
```

### New Method: check_link_local_address()

```cpp
void AutoInterface::check_link_local_address() {
    // Get current link-local address
    // Compare to stored _link_local_address
    // If different:
    //   1. Log the change
    //   2. Update _link_local_address, _link_local_address_str, _link_local_ip
    //   3. Close and rebind data socket
    //   4. Recalculate discovery token
    //   5. Set _carrier_changed = true
}
```

### ESP32 Implementation

For ESP32, getting the current link-local address:

```cpp
IPv6Address current_ip = WiFi.localIPv6();
// Compare to _link_local_ip
if (current_ip != _link_local_ip) {
    // Address changed!
    WARNING("AutoInterface: Link-local address changed from " + _link_local_address_str + " to " + ...);

    // Update stored addresses
    _link_local_ip = current_ip;
    for (int i = 0; i < 16; i++) {
        ((uint8_t*)&_link_local_address)[i] = current_ip[i];
    }
    _link_local_address_str = ipv6_to_compressed_string((const uint8_t*)&_link_local_address);

    // Rebind data socket
    if (_data_socket > -1) {
        close(_data_socket);
        _data_socket = -1;
    }
    setup_data_socket();

    // Recalculate discovery token
    calculate_discovery_token();

    // Signal change to transport layer
    _carrier_changed = true;
}
```

### POSIX Implementation

For POSIX, re-calling `get_link_local_address()` and comparing:

```cpp
struct in6_addr old_addr = _link_local_address;
std::string old_addr_str = _link_local_address_str;

// Temporarily clear to force refresh
memset(&_link_local_address, 0, sizeof(_link_local_address));

if (get_link_local_address()) {
    // Check if changed
    if (memcmp(&old_addr, &_link_local_address, sizeof(old_addr)) != 0) {
        WARNING("AutoInterface: Link-local address changed from " + old_addr_str + " to " + _link_local_address_str);

        // Rebind data socket
        if (_data_socket > -1) {
            close(_data_socket);
            _data_socket = -1;
        }
        setup_data_socket();

        // Recalculate discovery token
        calculate_discovery_token();

        // Signal change
        _carrier_changed = true;
    }
} else {
    // Lost address entirely
    WARNING("AutoInterface: Lost link-local address");
    _link_local_address = old_addr;  // Restore for now
    _link_local_address_str = old_addr_str;
}
```

### Integration in loop()

```cpp
void AutoInterface::loop() {
    // ... existing code ...

    // Periodic peer job (every 4 seconds)
    if (now - _last_peer_job >= PEER_JOB_INTERVAL) {
        check_link_local_address();
        _last_peer_job = now;
    }
}
```

## Requirements Coverage

| Requirement | Implementation |
|-------------|----------------|
| NET-01: Detect link-local address changes | `check_link_local_address()` compares current to stored address |
| NET-02: Restart data listener on change | Close `_data_socket`, call `setup_data_socket()` |
| NET-03: Track timeout state | Already done in Phase 2 (`_timed_out` flag) |

## Files to Modify

1. **AutoInterface.h**
   - Add `PEER_JOB_INTERVAL` constant
   - Add `_last_peer_job` member variable
   - Declare `check_link_local_address()` method

2. **AutoInterface.cpp**
   - Implement `check_link_local_address()` for ESP32
   - Implement `check_link_local_address()` for POSIX
   - Call from `loop()` every PEER_JOB_INTERVAL

## Key Implementation Notes

1. **Don't rebind discovery socket** - Only the data socket needs rebinding. Discovery socket is bound to multicast address which doesn't change.

2. **Unicast discovery socket** - Should also be rebound since it's bound to link-local. Close and recreate in check_link_local_address().

3. **Token recalculation** - Critical! Discovery token is hash of (group_id + link_local_address_str). If address changes, token must be recalculated or peers won't recognize us.

4. **Peer table** - Existing peers remain valid (their addresses didn't change). No need to clear peer table.

5. **ESP32 WiFi.localIPv6()** - Can return all zeros if WiFi disconnected. Handle this case gracefully.

---
*Research completed: 2025-01-25*
