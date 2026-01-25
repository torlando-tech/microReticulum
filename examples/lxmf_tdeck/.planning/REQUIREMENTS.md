# Requirements: AutoInterface Parity

**Defined:** 2025-01-25
**Core Value:** AutoInterface must reliably discover and maintain peer connections

## v1 Requirements

### Discovery

- [ ] **DISC-01**: AutoInterface sends unicast discovery tokens to known peers on port discovery_port+1
- [ ] **DISC-02**: Unicast discovery sent every reverse_peering_interval (3.25 × ANNOUNCE_INTERVAL = 5.2s)
- [ ] **DISC-03**: AutoInterface listens on unicast discovery port for direct peer tokens
- [ ] **DISC-04**: Peers discovered via unicast are added/refreshed same as multicast

### Echo Tracking

- [ ] **ECHO-01**: AutoInterface tracks when own multicast packets are received (multicast echo)
- [ ] **ECHO-02**: AutoInterface logs "Carrier lost" warning after MCAST_ECHO_TIMEOUT (6.5s) with no echo
- [ ] **ECHO-03**: AutoInterface logs firewall warning if no initial echo ever received
- [ ] **ECHO-04**: AutoInterface sets carrier_changed flag on carrier state transitions

### Network Handling

- [ ] **NET-01**: AutoInterface detects link-local IPv6 address changes
- [ ] **NET-02**: AutoInterface restarts data listener when link-local address changes
- [ ] **NET-03**: AutoInterface tracks per-interface timeout state

### Constants (Pre-Fixed)

- [x] **CONST-01**: PEERING_TIMEOUT = 22.0s
- [x] **CONST-02**: ANNOUNCE_INTERVAL = 1.6s
- [x] **CONST-03**: _data_socket_ok correctly tracks socket state
- [x] **CONST-04**: Low memory threshold = 8KB with proper logging

## Out of Scope

| Feature | Reason |
|---------|--------|
| Multi-interface support | ESP32 typically has single WiFi interface |
| Spawned peer interfaces | C++ uses internal peer list, valid approach |
| PEER_JOB_INTERVAL thread | Using inline loop() approach instead |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| DISC-01 | Phase 1 | Pending |
| DISC-02 | Phase 1 | Pending |
| DISC-03 | Phase 1 | Pending |
| DISC-04 | Phase 1 | Pending |
| ECHO-01 | Phase 2 | Pending |
| ECHO-02 | Phase 2 | Pending |
| ECHO-03 | Phase 2 | Pending |
| ECHO-04 | Phase 2 | Pending |
| NET-01 | Phase 3 | Pending |
| NET-02 | Phase 3 | Pending |
| NET-03 | Phase 3 | Pending |
| CONST-01 | Pre-work | Complete |
| CONST-02 | Pre-work | Complete |
| CONST-03 | Pre-work | Complete |
| CONST-04 | Pre-work | Complete |

**Coverage:**
- v1 requirements: 11 new + 4 pre-fixed = 15 total
- Mapped to phases: 11
- Pre-completed: 4 ✓

---
*Requirements defined: 2025-01-25*
