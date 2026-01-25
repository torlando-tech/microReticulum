# Roadmap: AutoInterface Parity v1.0

**Created:** 2025-01-25
**Milestone:** v1.0 - Full Python RNS AutoInterface parity

## Overview

| Phase | Name | Goal | Requirements |
|-------|------|------|--------------|
| 1 | Unicast Discovery | Reliable peer maintenance via direct tokens | DISC-01, DISC-02, DISC-03, DISC-04 |
| 2 | Echo Tracking | Carrier detection and diagnostics | ECHO-01, ECHO-02, ECHO-03, ECHO-04 |
| 3 | Network Handling | Address change resilience | NET-01, NET-02, NET-03 |

## Phase Details

### Phase 1: Unicast Discovery

**Goal:** Implement reverse peering so peers maintain connections even when multicast is unreliable.

**Plans:** 1 plan

Plans:
- [ ] 01-01-PLAN.md - Implement unicast discovery socket, send/receive methods, loop integration

**Requirements:**
- DISC-01: Send unicast discovery tokens to known peers on port discovery_port+1
- DISC-02: Send every reverse_peering_interval (5.2s)
- DISC-03: Listen on unicast discovery port
- DISC-04: Add/refresh peers from unicast same as multicast

**Success Criteria:**
1. T-Deck sends unicast discovery to each known peer every ~5.2 seconds
2. T-Deck receives and processes unicast discovery from phones
3. Peer connections maintained even when multicast is dropped
4. Interoperability verified with Columba phones

**Implementation Notes:**
- Add `_unicast_discovery_port` = `_discovery_port + 1`
- Add `_unicast_discovery_socket` for listening
- Add `_reverse_peering_interval` = `ANNOUNCE_INTERVAL * 3.25`
- Add `_last_reverse_announce` per peer tracking
- Implement `reverse_announce()` method
- Update `process_discovery()` to handle unicast socket

---

### Phase 2: Echo Tracking

**Goal:** Detect carrier loss and provide diagnostics when multicast isn't working.

**Requirements:**
- ECHO-01: Track multicast echo timestamps
- ECHO-02: Log carrier lost after 6.5s timeout
- ECHO-03: Log firewall warning if no initial echo
- ECHO-04: Set carrier_changed flag

**Success Criteria:**
1. AutoInterface logs when own multicast is received back
2. Warning logged if no echo received for 6.5 seconds
3. Error logged if multicast echo never received (firewall/network issue)
4. carrier_changed flag set on state transitions

**Implementation Notes:**
- Add `MCAST_ECHO_TIMEOUT = 6.5` constant
- Add `_last_multicast_echo` timestamp
- Add `_initial_echo_received` flag
- Add `_carrier_changed` flag
- Update `add_or_refresh_peer()` to update echo timestamp when own address detected
- Add echo timeout check in `loop()`

---

### Phase 3: Network Handling

**Goal:** Handle network changes gracefully.

**Requirements:**
- NET-01: Detect link-local address changes
- NET-02: Restart data listener on change
- NET-03: Track timeout state

**Success Criteria:**
1. AutoInterface detects when IPv6 link-local changes
2. Data socket rebound to new address automatically
3. timed_out state tracked per-interface
4. Logging indicates network state changes

**Implementation Notes:**
- Add `check_link_local_address()` method
- Call in `loop()` every PEER_JOB_INTERVAL (4s)
- If changed: close data socket, rebind, update token
- Add `_timed_out` flag for tracking

---

## Dependencies

```
Phase 1 (Unicast Discovery)
    |
Phase 2 (Echo Tracking) - independent, can parallelize
    |
Phase 3 (Network Handling) - depends on stable discovery
```

## Testing Strategy

Each phase should be verified by:
1. Building and flashing to T-Deck
2. Monitoring serial logs for expected behavior
3. Testing with Columba phones on same network
4. Verifying announces reach all peers
5. Deliberately degrading multicast to test resilience

---
*Roadmap created: 2025-01-25*
