# Phase 2: Echo Tracking - Research

**Researched:** 2026-01-25
**Domain:** IPv6 multicast echo detection and carrier diagnostics
**Confidence:** HIGH

## Summary

This phase implements multicast echo tracking to detect carrier loss and diagnose network/firewall issues in AutoInterface. The Python RNS reference implementation uses two dictionaries (`multicast_echoes` and `initial_echoes`) to track when an interface receives its own multicast announcements back. The echo detection mechanism is already partially implemented in the C++ version - it detects own echoes and logs them, but doesn't track timestamps or diagnose problems.

The implementation is straightforward: store timestamp when own multicast is received, check if timeout (6.5 seconds) has elapsed since last echo, and distinguish between "initial echo never received" (firewall/network issue) vs "echo stopped" (carrier lost). The `carrier_changed` flag signals Transport layer when interface state transitions occur.

**Primary recommendation:** Add per-interface echo timestamp tracking to existing `add_or_refresh_peer()` detection logic, implement timeout checking in main loop, and set `carrier_changed` flag on state transitions.

## Standard Stack

The established patterns for this domain:

### Core Components (Already in Codebase)
| Component | Current State | Purpose |
|-----------|---------------|---------|
| `add_or_refresh_peer()` | Detects own echo, logs and returns | Point where own multicast is received |
| `_link_local_address` / `_link_local_ip` | Exists | Used to identify own announcements |
| `RNS::Utilities::OS::time()` | Available | Timestamp source (matches Python `time.time()`) |
| `_peers` vector | Exists | Stores remote peers, excludes local |

### New Components Needed
| Component | Purpose | Type |
|-----------|---------|------|
| `_multicast_echo_ts` | Last echo timestamp | `double` (seconds) |
| `_initial_echo_received` | Flag: ever received echo | `bool` |
| `_carrier_ok` | Current carrier state | `bool` |
| `_carrier_changed` | Transport notification flag | `bool` |
| `MCAST_ECHO_TIMEOUT` | Timeout constant (6.5s) | `static constexpr double` |

### Python RNS Reference Pattern
```python
# AutoInterface.py initialization (line 130-131, 240)
self.multicast_echoes = {}     # {ifname: timestamp}
self.initial_echoes = {}       # {ifname: timestamp}
self.multicast_echoes[ifname] = time.time()

# Echo detection in add_peer() (line 521-522)
if addr in self.link_local_addresses:
    self.multicast_echoes[ifname] = time.time()
    if not ifname in self.initial_echoes:
        self.initial_echoes[ifname] = time.time()

# Timeout checking in peer_jobs() (line 450-467)
last_multicast_echo = self.multicast_echoes.get(ifname, 0)
multicast_echo_received = ifname in self.initial_echoes

if now - last_multicast_echo > self.multicast_echo_timeout:
    if not self.timed_out_interfaces[ifname]:
        self.carrier_changed = True
        RNS.log("Carrier lost.", RNS.LOG_WARNING)
    self.timed_out_interfaces[ifname] = True
else:
    if self.timed_out_interfaces[ifname]:
        self.carrier_changed = True
        RNS.log("Carrier recovered", RNS.LOG_WARNING)
    self.timed_out_interfaces[ifname] = False

if not multicast_echo_received:
    RNS.log("No multicast echoes. Firewall may be blocking.", RNS.LOG_ERROR)
```

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Per-interface tracking | Single global echo timestamp | Python uses per-interface for multi-interface support; C++ currently single-interface |
| Boolean state tracking | Tri-state enum (UNKNOWN/UP/DOWN) | Boolean sufficient for current needs |
| `carrier_changed` flag | Callback mechanism | Flag matches Python RNS pattern |

## Architecture Patterns

### Pattern 1: Echo Timestamp Update in Detection Path
**What:** Update echo timestamp when own multicast is detected
**When to use:** In `add_or_refresh_peer()` where own address is already detected
**Example:**
```cpp
// AutoInterface.cpp - add_or_refresh_peer()
void AutoInterface::add_or_refresh_peer(const IPv6Address& addr, double timestamp) {
    // Check if this is our own address
    if (addr == _link_local_ip) {
        // EXISTING: Already detects own echo
        DEBUG("AutoInterface: Received own multicast echo");

        // NEW: Track echo timestamp
        _multicast_echo_ts = timestamp;
        if (!_initial_echo_received) {
            _initial_echo_received = true;
            INFO("AutoInterface: Initial multicast echo received");
        }
        return;
    }
    // ... rest of peer handling
}
```

### Pattern 2: Timeout Checking in Main Loop
**What:** Periodically check if echo timeout has elapsed
**When to use:** In `loop()` after announce/discovery processing
**Example:**
```cpp
// AutoInterface.cpp - loop()
void AutoInterface::loop() {
    // ... existing announce, discovery, data processing

    // Check multicast echo timeout
    check_echo_timeout();
}

void AutoInterface::check_echo_timeout() {
    double now = RNS::Utilities::OS::time();

    // Timeout check (only if we've sent at least one announce)
    if (_last_announce > 0) {
        bool carrier_ok = (now - _multicast_echo_ts) <= MCAST_ECHO_TIMEOUT;

        // Detect state transitions
        if (carrier_ok != _carrier_ok) {
            _carrier_ok = carrier_ok;
            _carrier_changed = true;

            if (carrier_ok) {
                WARNING("AutoInterface: Carrier recovered");
            } else {
                WARNING("AutoInterface: Multicast echo timeout. Carrier lost.");
            }
        }

        // Firewall diagnostic (only log once per session)
        static bool firewall_warning_logged = false;
        if (!_initial_echo_received && !firewall_warning_logged) {
            ERROR("AutoInterface: No multicast echoes received. "
                  "The networking hardware or a firewall may be blocking multicast traffic.");
            firewall_warning_logged = true;
        }
    }
}
```

### Pattern 3: Carrier Changed Flag Access
**What:** Public accessor for Transport to check carrier state changes
**When to use:** Transport layer needs to react to interface state changes
**Example:**
```cpp
// AutoInterface.h
class AutoInterface : public RNS::InterfaceImpl {
public:
    // Carrier state tracking (matches Python RNS)
    bool carrier_changed() {
        bool changed = _carrier_changed;
        _carrier_changed = false;  // Clear flag on read
        return changed;
    }

    bool carrier_ok() const { return _carrier_ok; }

private:
    double _multicast_echo_ts = 0.0;
    bool _initial_echo_received = false;
    bool _carrier_ok = true;
    bool _carrier_changed = false;
};
```

### Pattern 4: Initialization State
**What:** Proper initial state before first announce
**When to use:** In `start()` after socket setup
**Example:**
```cpp
// AutoInterface.cpp - start()
bool AutoInterface::start() {
    // ... existing socket setup

    // Initialize echo tracking
    _multicast_echo_ts = RNS::Utilities::OS::time();  // Start optimistic
    _initial_echo_received = false;
    _carrier_ok = true;  // Assume carrier up initially
    _carrier_changed = false;

    _online = true;
    return true;
}
```

### Anti-Patterns to Avoid
- **Logging on every loop iteration:** Only log on state transitions (carrier lost/recovered)
- **Checking timeout too frequently:** Only check once per loop iteration, not per packet
- **Spamming firewall warning:** Log firewall diagnostic once per session, not repeatedly
- **Clearing echo timestamp on timeout:** Keep last timestamp for diagnostics

## Don't Hand-Roll

Problems that look simple but have existing solutions:

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Timestamp source | Custom millis() wrapper | `RNS::Utilities::OS::time()` | Already abstracted, consistent with Python |
| State change detection | Event queue system | Boolean flag with accessor | Matches Python RNS pattern exactly |
| Echo detection | Packet inspection | Existing address comparison in `add_or_refresh_peer()` | Already implemented and working |
| Per-interface tracking | Multiple AutoInterface instances | Single-interface initially | C++ implementation uses internal peer list, not spawned interfaces |

**Key insight:** Python RNS uses `multicast_echoes` dict for multi-interface support, but C++ AutoInterface currently manages a single interface. Phase 2 can use simple member variables instead of maps. Future multi-interface support would require refactoring to per-interface state.

## Common Pitfalls

### Pitfall 1: Checking Timeout Before First Announce
**What goes wrong:** False carrier loss warning on startup
**Why it happens:** Echo timestamp is 0, timeout check triggers immediately
**How to avoid:**
- Initialize `_multicast_echo_ts` to current time in `start()`
- Only check timeout if `_last_announce > 0` (announce has been sent)
- Allow ~1-2 announce intervals for initial echo before warning
**Warning signs:** "Carrier lost" log immediately after interface start

### Pitfall 2: Firewall Warning Spam
**What goes wrong:** ERROR log repeats every 4 seconds (peer_job_interval)
**Why it happens:** Checking `!_initial_echo_received` without tracking if warning logged
**How to avoid:**
- Use static bool `firewall_warning_logged` to log once per session
- Or only check after reasonable time (e.g., 3-4 announce intervals = ~5-6 seconds)
- Don't repeat warning once logged
**Warning signs:** Log spam: "No multicast echoes received..." every few seconds

### Pitfall 3: Race Condition in Echo Update
**What goes wrong:** Echo timestamp updated from ISR/different thread context
**Why it happens:** Socket receive may happen in different context than main loop
**How to avoid:**
- Current implementation: All socket processing in main loop (non-blocking sockets)
- No additional synchronization needed for single-threaded loop model
- If threading added later: protect echo timestamp with mutex
**Warning signs:** Inconsistent carrier state, spurious timeouts

### Pitfall 4: Not Clearing carrier_changed Flag
**What goes wrong:** Transport sees carrier change repeatedly
**Why it happens:** Flag is set but never cleared
**How to avoid:**
- Clear flag on read in `carrier_changed()` accessor
- Or use atomic exchange pattern
- Document that caller must clear flag after handling
**Warning signs:** Transport reacting to same state change multiple times

### Pitfall 5: Timeout During Normal Operation Gap
**What goes wrong:** Carrier lost warning during expected silence (no traffic)
**Why it happens:** Echo depends on sending announcements (1.6s interval)
**How to avoid:**
- Timeout (6.5s) is ~4x announce interval - normal gaps won't trigger
- Don't reduce timeout below 3x announce interval
- Keep announce interval consistent (don't skip announces)
**Warning signs:** Spurious "carrier lost" warnings that recover immediately

## Code Examples

Verified patterns from official sources:

### Complete Echo Tracking in add_or_refresh_peer()
```cpp
// Source: Python RNS AutoInterface.py lines 513-525
// Adapted for C++ single-interface implementation

#ifdef ARDUINO
void AutoInterface::add_or_refresh_peer(const IPv6Address& addr, double timestamp) {
    // Check if this is our own address (IPv6Address == properly compares all 16 bytes)
    if (addr == _link_local_ip) {
        // Update echo timestamp
        _multicast_echo_ts = timestamp;

        // Track initial echo received
        if (!_initial_echo_received) {
            _initial_echo_received = true;
            INFO("AutoInterface: Initial multicast echo received - multicast is working");
        }

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

    // Add new peer (existing code continues...)
    AutoInterfacePeer new_peer(addr, _data_port, timestamp);
    _peers.push_back(new_peer);
    INFO("AutoInterface: Added new peer " + new_peer.address_string());
}
#endif
```

### Echo Timeout Checking Logic
```cpp
// Source: Python RNS AutoInterface.py lines 449-467
// Adapted for C++ member variables

void AutoInterface::check_echo_timeout() {
    double now = RNS::Utilities::OS::time();

    // Only check if we've started announcing
    if (_last_announce == 0) {
        return;  // Haven't sent first announce yet
    }

    // Calculate time since last echo
    double echo_age = now - _multicast_echo_ts;
    bool carrier_ok = (echo_age <= MCAST_ECHO_TIMEOUT);

    // Detect carrier state transitions
    if (carrier_ok != _carrier_ok) {
        _carrier_ok = carrier_ok;
        _carrier_changed = true;

        if (carrier_ok) {
            WARNING("AutoInterface: Carrier recovered on interface");
        } else {
            WARNING("AutoInterface: Multicast echo timeout for interface. Carrier lost.");
        }
    }

    // One-time firewall diagnostic (after grace period)
    static bool firewall_warning_logged = false;
    double startup_grace = ANNOUNCE_INTERVAL * 3.0;  // ~5 seconds

    if (!_initial_echo_received &&
        (now - _last_announce) > startup_grace &&
        !firewall_warning_logged) {
        ERROR("AutoInterface: No multicast echoes received. "
              "The networking hardware or a firewall may be blocking multicast traffic.");
        firewall_warning_logged = true;
    }
}
```

### Constants in Header
```cpp
// AutoInterface.h
class AutoInterface : public RNS::InterfaceImpl {
public:
    // Protocol constants (match Python RNS)
    static const uint16_t DEFAULT_DISCOVERY_PORT = 29716;
    static const uint16_t DEFAULT_DATA_PORT = 42671;
    static constexpr const char* DEFAULT_GROUP_ID = "reticulum";
    static constexpr double PEERING_TIMEOUT = 22.0;      // seconds (matches Python RNS)
    static constexpr double ANNOUNCE_INTERVAL = 1.6;     // seconds (matches Python RNS)
    static constexpr double MCAST_ECHO_TIMEOUT = 6.5;    // seconds (NEW - matches Python RNS line 64)
    static constexpr double REVERSE_PEERING_INTERVAL = ANNOUNCE_INTERVAL * 3.25;  // ~5.2 seconds

private:
    // Echo tracking (matches Python self.multicast_echoes / self.initial_echoes)
    double _multicast_echo_ts = 0.0;        // Timestamp of last own echo received
    bool _initial_echo_received = false;     // True once first echo received
    bool _carrier_ok = true;                 // Current carrier state
    bool _carrier_changed = false;           // Flag for Transport layer
};
```

### Integration in Main Loop
```cpp
// AutoInterface.cpp - loop()
void AutoInterface::loop() {
    if (!_online) return;

    double now = RNS::Utilities::OS::time();

    // Send periodic discovery announce
    if (now - _last_announce >= ANNOUNCE_INTERVAL) {
        send_announce();
        _last_announce = now;
    }

    // Process incoming discovery packets (multicast)
    process_discovery();

    // Process incoming unicast discovery packets (reverse peering)
    process_unicast_discovery();

    // Send reverse peering to known peers
    send_reverse_peering();

    // Process incoming data packets
    process_data();

    // NEW: Check multicast echo timeout
    check_echo_timeout();

    // Expire stale peers
    expire_stale_peers();

    // Expire old deque entries
    expire_deque_entries();
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| No echo tracking | Echo timestamp + timeout | Python RNS ~2019 | Enables carrier loss detection |
| Assume carrier always up | Detect via multicast echo | Python RNS ~2019 | Diagnose network issues |
| No firewall diagnostics | Log if initial echo never received | Python RNS ~2020 | User-actionable error message |
| Interface always online | `carrier_changed` flag | Python RNS ~2019 | Transport can react to state changes |

**Current state (microReticulum C++):**
- Echo detection: Implemented (detects own address, logs and returns)
- Echo tracking: NOT implemented (no timestamp storage)
- Timeout checking: NOT implemented
- Carrier diagnostics: NOT implemented
- `carrier_changed` flag: NOT implemented

**After Phase 2:**
- All features will match Python RNS AutoInterface echo tracking behavior

## Open Questions

Things that couldn't be fully resolved:

1. **Transport Layer Usage of carrier_changed**
   - What we know: Python RNS sets flag, used for interface state tracking
   - What's unclear: How microReticulum Transport layer currently handles interface state
   - Recommendation: Implement flag for future Transport integration; document in header

2. **Multi-Interface Support Timeline**
   - What we know: Python RNS uses per-interface dicts, C++ uses single interface currently
   - What's unclear: When multi-interface support will be added to C++ version
   - Recommendation: Use simple member variables for Phase 2; refactor to map when multi-interface added

3. **Optimal Grace Period for Firewall Warning**
   - What we know: Need to wait for several announce cycles before warning
   - What's unclear: Exact delay before first warning (3 intervals? 4 intervals?)
   - Recommendation: Use 3x announce interval (~5 seconds) as grace period

## Sources

### Primary (HIGH confidence)
- Python RNS AutoInterface.py (lines 64, 130-131, 240, 450-467, 521-522) - Echo tracking reference implementation
- microReticulum AutoInterface.cpp (lines 1249-1251, 1274-1276) - Existing echo detection code
- microReticulum AutoInterface.h - Class structure and constants

### Secondary (MEDIUM confidence)
- Python RNS Git history - When echo tracking was added and why
- RNS documentation - Interface carrier detection

### Codebase (HIGH confidence - existing patterns)
- `/home/tyler/repos/public/microReticulum/examples/common/auto_interface/AutoInterface.cpp` - Existing patterns for peer management
- `/home/tyler/repos/public/microReticulum/examples/common/auto_interface/AutoInterface.h` - Constant definitions, class structure
- `/home/tyler/repos/public/microReticulum/src/Utilities/OS.h` - Time source (`OS::time()`)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - Direct Python RNS reference code analysis
- Architecture: HIGH - Patterns derived from existing C++ implementation and Python reference
- Pitfalls: HIGH - Known issues from Python RNS behavior and networking fundamentals

**Research date:** 2026-01-25
**Valid until:** 2026-03-25 (Echo tracking is stable, unlikely to change)
