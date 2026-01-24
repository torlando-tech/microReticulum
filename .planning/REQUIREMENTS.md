# Requirements: microReticulum v1.1

**Defined:** 2026-01-24
**Core Value:** Fix highest-priority stability issues from v1 audit for reliable extended operation

## v1.1 Requirements

Requirements for stability quick wins. All items are P1 from BACKLOG.md (WSJF ≥ 3.0).

### Memory Fixes

- [ ] **MEM-01**: Fix duplicate static definition in Persistence
  - Problem: DynamicJsonDocument defined in both header and cpp file (ODR violation)
  - Location: `src/Utilities/Persistence.h:475`, `src/Utilities/Persistence.cpp:7`
  - Fix: Single definition in cpp file, extern declaration in header
  - Verification: Clean compile with no linker warnings, single definition in binary

- [ ] **MEM-02**: Pre-allocate ResourceData vectors
  - Problem: `_parts` and `_hashmap` vectors grow during large file transfers
  - Location: `src/ResourceData.h:59`, `src/ResourceData.h:68`
  - Fix: Reserve MAX_PARTS slots at construction
  - Verification: No vector reallocations during resource transfers

### Concurrency Fixes

- [ ] **CONC-01**: Enable Task Watchdog Timer for application tasks
  - Problem: TWDT not configured, task starvation/deadlock undetected
  - Location: `sdkconfig.defaults`, task creation sites
  - Fix: Enable TWDT config, subscribe critical tasks with esp_task_wdt_add()
  - Verification: TWDT triggers on simulated deadlock, normal operation unaffected

- [ ] **CONC-02**: Fix LXStamper yield frequency
  - Problem: Yields only every 100 rounds, causing UI freeze during stamp generation
  - Location: LXStamper stamp generation loop
  - Fix: Yield every 10 rounds, reset watchdog in yield
  - Verification: UI responsive during stamp generation, stamps still generate correctly

- [ ] **CONC-03**: Add mutex protection to BLE pending queues
  - Problem: `_pending_handshakes` and `_pending_data` accessed without mutex
  - Location: BLEInterface callback handlers
  - Fix: Add lock_guard in callbacks
  - Verification: No crashes during concurrent BLE operations

## Future Requirements (v1.2+)

Deferred P2/P3 issues from backlog:

### P2 Issues (WSJF 2.0)
- MEM-M1: Bytes newData make_shared pattern
- MEM-M2: PacketReceipt default constructor allocates
- MEM-M3: DynamicJsonDocument in Persistence (ArduinoJson 7 migration)
- CONC-M1-M3: Missing LVGL_LOCK in screen constructors
- CONC-M5-M9: Various mutex and timeout improvements

### P3 Issues (WSJF 0.8-1.4)
- MEM-H1-H4: Deeper allocation pattern refactoring
- CONC-H4: Shutdown during active operations
- Various Low severity items

## Out of Scope

| Feature | Reason |
|---------|--------|
| Pool allocators | High effort (P3), requires v1.1 stability first |
| ArduinoJson 7 full migration | Separate milestone, broader impact |
| New features | Stability focus for this milestone |
| Boot time optimization | Already at 5.3s, acceptable for now |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| MEM-01 | Phase 6 | Pending |
| MEM-02 | Phase 6 | Pending |
| CONC-01 | Phase 6 | Pending |
| CONC-02 | Phase 6 | Pending |
| CONC-03 | Phase 6 | Pending |

**Coverage:**
- v1.1 requirements: 5 total
- Mapped to phases: 5
- Unmapped: 0 ✓

---
*Requirements defined: 2026-01-24*
*Last updated: 2026-01-24 after initial definition*
