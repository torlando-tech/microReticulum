# Requirements: microReticulum v1.2 Stability Complete

**Defined:** 2026-01-24
**Core Value:** Reliable firmware operation for extended periods without crashes or performance degradation.

## v1.2 Requirements

Complete all P2 and P3 stability issues from the v1 audit. Each requirement maps to a backlog issue.

### Memory Allocation (P2)

- [ ] **MEM-M1**: Bytes newData uses make_shared pattern (single allocation)
- [ ] **MEM-M2**: PacketReceipt default constructor defers allocation until use
- [ ] **MEM-M3**: Persistence uses JsonDocument (ArduinoJson 7 API fully migrated)

### LVGL Thread-Safety (P2)

- [ ] **CONC-M1**: SettingsScreen constructor/destructor uses LVGL_LOCK
- [ ] **CONC-M2**: ComposeScreen constructor/destructor uses LVGL_LOCK
- [ ] **CONC-M3**: AnnounceListScreen constructor/destructor uses LVGL_LOCK
- [ ] **CONC-M7**: LVGL mutex uses debug timeout (5s) to detect deadlocks

### BLE/NimBLE (P2)

- [ ] **CONC-M5**: Connection mutex timeout failures are logged
- [ ] **CONC-M6**: Discovered devices cache bounded with LRU eviction

### Infrastructure (P2)

- [ ] **CONC-M8**: Audio I2S write uses reasonable timeout (not portMAX_DELAY)
- [ ] **CONC-M9**: Mutex ordering documented in CONTRIBUTING.md

### Memory Optimization (P3)

- [ ] **MEM-H1**: Bytes COW copy uses pool or arena allocator
- [ ] **MEM-H2**: Packet Object uses pool allocator (16-32 slots)
- [ ] **MEM-H3**: Packet fixed-size members use inline buffers (save ~150 bytes/packet)
- [ ] **MEM-H4**: PacketReceipt allocation is lazy (only for non-NONE types)
- [ ] **MEM-L1**: toHex reserves string capacity upfront

### Concurrency Hardening (P3)

- [ ] **CONC-H4**: BLE shutdown waits for active operations to complete
- [ ] **CONC-M4**: Soft reset fully releases NimBLE state (or uses hard recovery)
- [ ] **CONC-L1**: Native GAP handler volatile usage is documented
- [ ] **CONC-L2**: 50ms error recovery delay is documented with rationale
- [ ] **CONC-L4**: Debug builds use mutex timeout to detect stuck tasks

## Out of Scope

| Feature | Reason |
|---------|--------|
| P4 backlog items | Deferred - low impact, address opportunistically |
| New features | Stability first, features in v1.3+ |
| Link watchdog | P4 - medium effort, low severity |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| MEM-M1 | Phase 7 | Pending |
| MEM-M2 | Phase 7 | Pending |
| MEM-M3 | Phase 7 | Pending |
| CONC-M1 | Phase 7 | Pending |
| CONC-M2 | Phase 7 | Pending |
| CONC-M3 | Phase 7 | Pending |
| CONC-M7 | Phase 7 | Pending |
| CONC-M5 | Phase 7 | Pending |
| CONC-M6 | Phase 7 | Pending |
| CONC-M8 | Phase 7 | Pending |
| CONC-M9 | Phase 7 | Pending |
| MEM-H1 | Phase 8 | Pending |
| MEM-H2 | Phase 8 | Pending |
| MEM-H3 | Phase 8 | Pending |
| MEM-H4 | Phase 8 | Pending |
| MEM-L1 | Phase 8 | Pending |
| CONC-H4 | Phase 8 | Pending |
| CONC-M4 | Phase 8 | Pending |
| CONC-L1 | Phase 8 | Pending |
| CONC-L2 | Phase 8 | Pending |
| CONC-L4 | Phase 8 | Pending |

**Coverage:**
- v1.2 requirements: 21 total
- Mapped to phases: 21
- Unmapped: 0 ✓

---
*Requirements defined: 2026-01-24*
*Last updated: 2026-01-24 after initial definition*
