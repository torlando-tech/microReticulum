# Phase 2 Research: Echo Tracking

## Objective

Implement multicast echo tracking to detect carrier loss and provide diagnostics when multicast isn't working.

## Python RNS Reference Analysis

### Constants (AutoInterface.py:64)

```python
MCAST_ECHO_TIMEOUT = 6.5  # seconds
```

### State Variables (AutoInterface.py:130-137)

```python
self.multicast_echoes = {}      # ifname -> timestamp of last echo received
self.initial_echoes = {}        # ifname -> timestamp of first echo (presence = echo ever received)
self.timed_out_interfaces = {}  # ifname -> bool (True if currently timed out)
self.carrier_changed = False    # Flag for transport layer
```

### Echo Detection (AutoInterface.py:518-522)

When a discovery packet is received from our own address:

```python
if ifname != None:
    self.multicast_echoes[ifname] = time.time()
    if not ifname in self.initial_echoes: self.initial_echoes[ifname] = time.time()
```

### Timeout Check Logic (AutoInterface.py:449-469)

In the peer job loop (every 4 seconds):

```python
last_multicast_echo = 0
multicast_echo_received = False
if ifname in self.multicast_echoes: last_multicast_echo = self.multicast_echoes[ifname]
if ifname in self.initial_echoes:   multicast_echo_received = True

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

if not multicast_echo_received:
    RNS.log(f"{self} No multicast echoes received on {ifname}. The networking hardware or a firewall may be blocking multicast traffic.", RNS.LOG_ERROR)
```

## C++ Implementation Plan

### Current Code (AutoInterface.cpp:963-968, ESP32)

```cpp
void AutoInterface::add_or_refresh_peer(const IPv6Address& addr, double timestamp) {
    // Check if this is our own address
    if (addr == _link_local_ip) {
        DEBUG("AutoInterface: Received own multicast echo - ignoring");
        return;
    }
    // ... rest of peer logic
}
```

This is the integration point - instead of just returning, we update echo timestamps.

### New Constants (AutoInterface.h)

```cpp
static constexpr double MCAST_ECHO_TIMEOUT = 6.5;  // seconds (matches Python RNS)
```

### New Member Variables (AutoInterface.h)

```cpp
// Echo tracking
double _last_multicast_echo = 0;      // Timestamp of last echo received
bool _initial_echo_received = false;   // True if we've ever received an echo
bool _timed_out = false;               // True if echo timeout triggered
bool _carrier_changed = false;         // Flag for transport layer
```

### Modified add_or_refresh_peer() (Both Platforms)

```cpp
void AutoInterface::add_or_refresh_peer(const IPv6Address& addr, double timestamp) {
    // Check if this is our own address (multicast echo)
    if (addr == _link_local_ip) {
        // Update echo tracking
        _last_multicast_echo = timestamp;
        if (!_initial_echo_received) {
            _initial_echo_received = true;
            INFO("AutoInterface: Initial multicast echo received");
        }
        TRACE("AutoInterface: Multicast echo received");
        return;
    }
    // ... existing peer logic unchanged
}
```

### New check_echo_timeout() Method

```cpp
void AutoInterface::check_echo_timeout() {
    double now = RNS::Utilities::OS::time();

    // Check if echo has timed out
    if (_last_multicast_echo > 0 && (now - _last_multicast_echo) > MCAST_ECHO_TIMEOUT) {
        if (!_timed_out) {
            _carrier_changed = true;
            WARNING("AutoInterface: Multicast echo timeout. Carrier lost.");
        }
        _timed_out = true;
    } else if (_last_multicast_echo > 0) {
        if (_timed_out) {
            _carrier_changed = true;
            WARNING("AutoInterface: Carrier recovered");
        }
        _timed_out = false;
    }

    // Warning if no initial echo ever received (firewall/network issue)
    // Only log this once after a grace period (e.g., 3x announce interval)
    static bool firewall_warning_logged = false;
    if (!_initial_echo_received && !firewall_warning_logged &&
        (now - _last_announce > ANNOUNCE_INTERVAL * 3)) {
        ERROR("AutoInterface: No multicast echoes received. Firewall or network may be blocking multicast.");
        firewall_warning_logged = true;
    }
}
```

### Integration in loop()

```cpp
void AutoInterface::loop() {
    // ... existing code ...

    // Check multicast echo timeout
    check_echo_timeout();
}
```

### Public Accessor for Transport Layer

```cpp
// In header, public section:
bool carrier_changed() const { return _carrier_changed; }
void clear_carrier_changed() { _carrier_changed = false; }
bool is_timed_out() const { return _timed_out; }
```

## Key Implementation Notes

1. **Single Interface Simplification**: Python uses dicts for multi-interface. C++ uses single interface, so simple member variables suffice.

2. **Grace Period for Firewall Warning**: Don't log firewall error immediately at startup - wait at least 3 announce intervals (4.8 seconds) before warning, since first echo takes time.

3. **Static Flag for Once-Only Logging**: The firewall warning should only be logged once per session, use a static bool or member variable.

4. **Transport Integration**: The `carrier_changed` flag is for the Transport layer to detect interface state changes. For now, just implement the flag - Transport integration can be added later.

5. **Echo vs Peer Management**: The echo is received in process_discovery() which calls add_or_refresh_peer(). The echo tracking happens in add_or_refresh_peer() when own address is detected.

## Files to Modify

1. **AutoInterface.h**
   - Add `MCAST_ECHO_TIMEOUT` constant
   - Add `_last_multicast_echo`, `_initial_echo_received`, `_timed_out`, `_carrier_changed` members
   - Declare `check_echo_timeout()` method
   - Add `carrier_changed()`, `clear_carrier_changed()`, `is_timed_out()` accessors

2. **AutoInterface.cpp**
   - Modify `add_or_refresh_peer()` (both ESP32 and POSIX) to update echo timestamp
   - Add `check_echo_timeout()` implementation
   - Call `check_echo_timeout()` from `loop()`

## Requirements Coverage

| Requirement | Implementation |
|-------------|----------------|
| ECHO-01: Track multicast echo timestamps | `_last_multicast_echo` updated in `add_or_refresh_peer()` |
| ECHO-02: Log carrier lost after 6.5s timeout | `check_echo_timeout()` logs WARNING when timeout triggers |
| ECHO-03: Log firewall warning if no initial echo | `check_echo_timeout()` logs ERROR after grace period |
| ECHO-04: Set carrier_changed flag | `_carrier_changed = true` on state transitions |

---
*Research completed: 2025-01-25*
