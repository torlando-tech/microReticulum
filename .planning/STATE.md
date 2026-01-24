# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-01-24)

**Core value:** Reliable firmware operation for extended periods without crashes or performance degradation.
**Current focus:** Phase 8 - P3 Optimization & Hardening (COMPLETE + all gap closures + resilience)

## Current Position

Phase: 8 of 8 (P3 Optimization & Hardening)
Plan: 8 of 8 complete (including 08-06, 08-07, 08-08 gap closures)
Status: Phase complete - MEM-H1, MEM-H2, MEM-H3 all complete with resilience hardening
Last activity: 2026-01-24 -- Completed 08-08-PLAN.md (Production resilience)

Progress: [==========================] 30/30 plans (100% through v1.0-v1.2 + all gap closures)

## Milestones

- v1.0 Stability Audit -- shipped 2026-01-24
- v1.1 Stability Quick Wins -- shipped 2026-01-24
- v1.2 Stability Complete -- shipped 2026-01-24

## Performance Metrics

**Velocity:**
- Total plans completed: 30
- Average duration: ~27 min
- Total execution time: ~13.6 hours

**By Milestone:**

| Milestone | Phases | Plans | Duration |
|-----------|--------|-------|----------|
| v1.0 Stability Audit | 5 | 15 | ~11h |
| v1.1 Quick Wins | 1 | 2 | ~13m |
| v1.2 Stability Complete | 2 | 13 (5 phase 7, 8 phase 8) | ~2h |

## Accumulated Context

### Decisions

Decisions logged in PROJECT.md Key Decisions table.
Key decisions from v1.1:
- ArduinoJson 7 API for ODR compliance
- MAX_PARTS = 256 for ResourceData
- 10s TWDT timeout
- 10-round yield frequency for LXStamper
Key decisions from v1.2 (Phase 7):
- 5s debug timeout for LVGL mutex deadlock detection (CONC-M7)
- LVGL_LOCK pattern in screen constructors/destructors (CONC-M1, M2, M3)
- 2000ms timeout for I2S writes (CONC-M8)
- 16 device max for BLE discovered cache (CONC-M6)
- Connected devices protected from cache eviction
- 100ms mutex timeout with warning logging (CONC-M5)
- make_shared for Bytes single allocation (MEM-M1)
- Deferred PacketReceipt allocation (MEM-M2)
- Lock ordering documented: Transport -> BLE -> LVGL (CONC-M9)
Key decisions from Phase 8:
- WARNING log + continue waiting (not assert crash) for lvgl_task timeout (CONC-L4)
- Capacity reservation for Bytes::toHex (MEM-L1)
- VOLATILE RATIONALE comment pattern for callback/ISR synchronization (CONC-L1)
- DELAY RATIONALE comment pattern for timing-sensitive code (CONC-L2)
- 10ms = NimBLE scheduler tick as minimum polling interval
- 10s graceful shutdown timeout for BLE write operations (CONC-H4)
- RTC_NOINIT_ATTR for unclean shutdown flag persistence (CONC-H4)
- Soft reset performs full shutdown/reinit cycle (CONC-M4)
- ObjectPool spinlock on ESP32, mutex on native (MEM-H2)
- Inline buffers return Bytes by value for reduced fragmentation (MEM-H3)
- BytesPool integrated into Bytes allocation paths (MEM-H1 COMPLETE)
- Pool Data objects not raw buffers - eliminates control block + vector fragmentation
- Three tiers: 256/512/1024 bytes, 16 slots each = 48 pooled buffers
- Custom deleter returns Data to pool on refcount=0
- PacketObjectPool: 24 slots for Packet::Object (MEM-H2 COMPLETE)
- ReceiptObjectPool: 24 slots for PacketReceipt::Object (MEM-H2 COMPLETE)
- Variadic ObjectPool::allocate() for constructor args forwarding
- WARNINGF for pool exhaustion with full pool state (08-08 resilience)
- try/catch around heap fallback allocations (08-08 resilience)
- Static fallback counters per pool class for monitoring (08-08 resilience)

### Pending Todos

None.

### Blockers/Concerns

- Native build has pre-existing msgpack type compatibility issues in LXMessage.cpp/Link.cpp
- ESP32 builds have pre-existing missing lvgl.h for TDeck hardware

## Session Continuity

Last session: 2026-01-24
Stopped at: Completed 08-08-PLAN.md - Production resilience complete
Resume file: None

---
*Last updated: 2026-01-24 after 08-08 complete*
