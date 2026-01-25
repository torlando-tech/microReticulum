# AutoInterface: Python vs C++ Comparison

## Constants Comparison

| Constant | Python RNS | C++ (current) | Status |
|----------|-----------|---------------|--------|
| DEFAULT_DISCOVERY_PORT | 29716 | 29716 | ✓ Match |
| DEFAULT_DATA_PORT | 42671 | 42671 | ✓ Match |
| DEFAULT_GROUP_ID | "reticulum" | "reticulum" | ✓ Match |
| HW_MTU | 1196 | 1196 | ✓ Match |
| PEERING_TIMEOUT | 22.0s | 22.0s | ✓ Match (fixed) |
| ANNOUNCE_INTERVAL | 1.6s | 1.6s | ✓ Match (fixed) |
| PEER_JOB_INTERVAL | 4.0s | N/A | ✗ Missing |
| MCAST_ECHO_TIMEOUT | 6.5s | N/A | ✗ Missing |
| MULTI_IF_DEQUE_LEN | 48 | 48 | ✓ Match |
| MULTI_IF_DEQUE_TTL | 0.75s | 0.75s | ✓ Match |
| BITRATE_GUESS | 10Mbps | 10Mbps | ✓ Match |
| TOKEN_SIZE | 32 bytes | 32 bytes | ✓ Match |

## Feature Comparison

### 1. Peer Discovery

| Feature | Python RNS | C++ | Gap |
|---------|-----------|-----|-----|
| Multicast discovery | ✓ | ✓ | None |
| Unicast discovery port | discovery_port + 1 | N/A | **MISSING** |
| Reverse peering | Every 3.25 * ANNOUNCE_INTERVAL | N/A | **MISSING** |
| Own-echo detection | ✓ (multicast_echoes dict) | ✓ (returns early) | Partial |

### 2. Peer Management

| Feature | Python RNS | C++ | Gap |
|---------|-----------|-----|-----|
| Peer timeout | 22s | 22s | Fixed |
| Spawned interfaces | Creates AutoInterfacePeer per peer | Single interface with peer list | **ARCHITECTURE DIFF** |
| Peer job thread | Runs every 4s | Inline in loop() | Different approach |

### 3. Multicast Echo Tracking

| Feature | Python RNS | C++ | Gap |
|---------|-----------|-----|-----|
| Echo timeout detection | 6.5s timeout, logs "Carrier lost" | N/A | **MISSING** |
| Initial echo tracking | Logs warning if never received | N/A | **MISSING** |
| Carrier change flag | ✓ | N/A | **MISSING** |

### 4. Network Handling

| Feature | Python RNS | C++ | Gap |
|---------|-----------|-----|-----|
| Link-local address change detection | ✓ Restarts listener | N/A | **MISSING** |
| Multiple network interfaces | ✓ Per-interface sockets | Single interface | **ARCHITECTURE DIFF** |
| Interface timeout tracking | Per-interface timed_out_interfaces | N/A | **MISSING** |

## Critical Gaps to Address

### HIGH PRIORITY (affects reliability)

1. **Unicast Discovery (Reverse Peering)**
   - Python: Sends discovery tokens directly to known peers via unicast on port `discovery_port + 1`
   - Interval: `announce_interval * 3.25` = 5.2 seconds
   - Purpose: Maintains peering even when multicast is unreliable
   - C++: Only does multicast discovery

2. **Multicast Echo Tracking**
   - Python: Tracks when own multicast packets are received back
   - If no echo for 6.5s, logs "Carrier lost" warning
   - If never received, logs firewall/network warning
   - C++: Just ignores own-echoes, no tracking

### MEDIUM PRIORITY (affects diagnostics)

3. **Peer Job Thread**
   - Python: Dedicated thread runs every 4s for peer maintenance
   - Handles: peer timeouts, reverse peering, link-local changes
   - C++: All inline in loop()

4. **Link-Local Address Change Detection**
   - Python: Monitors for IPv6 address changes, restarts listener
   - C++: Assumes address is static

### LOW PRIORITY (architectural differences)

5. **Spawned Peer Interfaces**
   - Python: Creates separate Interface object per peer, registers with Transport
   - C++: Single interface manages peer list internally
   - Note: C++ approach is valid, just different

## Implementation Plan

### Phase 1: Unicast Discovery
- Add unicast_discovery_port member (discovery_port + 1)
- Add unicast discovery socket setup
- Implement reverse_announce() to send to known peers
- Add reverse_peering_interval tracking

### Phase 2: Multicast Echo Tracking
- Add multicast_echoes map per interface
- Add initial_echoes tracking
- Add MCAST_ECHO_TIMEOUT constant (6.5s)
- Log warnings on carrier loss/firewall issues

### Phase 3: Diagnostics
- Add timed_out_interfaces tracking
- Add carrier_changed flag
- Improve logging for network issues

---
*Research completed: Based on comparison of Python RNS AutoInterface.py vs C++ AutoInterface.cpp*
