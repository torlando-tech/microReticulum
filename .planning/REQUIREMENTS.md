# Requirements: microReticulum Stability Audit

**Defined:** 2026-01-23
**Core Value:** Identify and prioritize root causes of instability for reliable extended operation

## v1 Requirements

Requirements for comprehensive stability audit. Each maps to roadmap phases.

### Memory Management

- [x] **MEM-01**: Add heap monitoring (free heap, largest block, fragmentation %)
- [x] **MEM-02**: Add stack high water mark monitoring for all FreeRTOS tasks
- [x] **MEM-03**: Audit shared_ptr allocation patterns (make_shared vs new/shared_ptr)
- [x] **MEM-04**: Audit Packet/Bytes allocation frequency and size patterns
- [x] **MEM-05**: Audit ArduinoJson usage (Dynamic vs Static documents)
- [x] **MEM-06**: Verify PSRAM allocation strategy (large buffers using SPIRAM)
- [x] **MEM-07**: Document memory pools and their overflow handling

### Boot Performance

- [x] **BOOT-01**: Profile boot sequence with esp_timer instrumentation
- [x] **BOOT-02**: Audit PSRAM memory test configuration
- [x] **BOOT-03**: Audit flash mode configuration (QIO @ 80MHz)
- [x] **BOOT-04**: Audit log level during initialization
- [x] **BOOT-05**: Identify blocking operations in setup()

### Concurrency

- [x] **CONC-01**: Audit LVGL mutex usage for thread safety
- [x] **CONC-02**: Audit NimBLE init/deinit lifecycle (leak potential)
- [x] **CONC-03**: Verify all tasks feed watchdog appropriately
- [x] **CONC-04**: Audit mutex ordering for deadlock potential
- [x] **CONC-05**: Verify task stack sizes are adequate

### Deliverables

- [x] **DLVR-01**: Prioritized backlog with severity and fix recommendations
- [x] **DLVR-02**: Instrumentation code for ongoing monitoring
- [x] **DLVR-03**: Configuration recommendations (platformio.ini changes)

## v2 Requirements

Deferred to after audit findings are addressed. Tracked but not in current roadmap.

### Fixes and Refactoring

- **FIX-01**: Implement pool-backed allocation for Packet/Bytes (if MEM-04 confirms need)
- **FIX-02**: Migrate ArduinoJson to StaticJsonDocument (if MEM-05 confirms need)
- **FIX-03**: Implement staged boot with splash screen (if BOOT-05 identifies blocking ops)
- **FIX-04**: Add graceful degradation on low memory threshold

### Validation

- **VAL-01**: 48-72 hour soak test after fixes applied
- **VAL-02**: Stress testing under high message volume

## Out of Scope

Explicitly excluded. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| New functionality | Stability audit only, no feature work |
| Python reference implementation | Focus is C++ codebase |
| Hardware driver debugging | Unless directly causing stability issues |
| Test coverage improvements | Unless directly related to identified issues |
| Performance optimization beyond boot | Focus on stability, not throughput |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| MEM-01 | Phase 1 | Complete |
| MEM-02 | Phase 1 | Complete |
| MEM-03 | Phase 3 | Complete |
| MEM-04 | Phase 3 | Complete |
| MEM-05 | Phase 3 | Complete |
| MEM-06 | Phase 3 | Complete |
| MEM-07 | Phase 3 | Complete |
| BOOT-01 | Phase 2 | Complete |
| BOOT-02 | Phase 2 | Complete |
| BOOT-03 | Phase 2 | Complete |
| BOOT-04 | Phase 2 | Complete |
| BOOT-05 | Phase 2 | Complete |
| CONC-01 | Phase 4 | Complete |
| CONC-02 | Phase 4 | Complete |
| CONC-03 | Phase 4 | Complete |
| CONC-04 | Phase 4 | Complete |
| CONC-05 | Phase 4 | Complete |
| DLVR-01 | Phase 5 | Complete |
| DLVR-02 | Phase 1 | Complete |
| DLVR-03 | Phase 2 | Complete |

**Coverage:**
- v1 requirements: 20 total
- Mapped to phases: 20
- Unmapped: 0

---
*Requirements defined: 2026-01-23*
*Last updated: 2026-01-24 — Milestone 1 complete (20/20 requirements)*
