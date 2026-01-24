# Project Milestones: microReticulum

## v1.2 Stability Complete (Shipped: 2026-01-24)

**Delivered:** Completed all P2 and P3 stability issues from v1 audit, making firmware production-ready.

**Phases completed:** 7-8 (13 plans total)

**Key accomplishments:**

- LVGL thread safety: All screen constructors/destructors protected with LVGL_LOCK, 5s debug timeout
- Memory pool architecture: BytesPool (4-tier) and ObjectPool for Packet/Receipt eliminating heap fragmentation
- Packet inline buffers: Fixed-size fields use inline storage saving ~150 bytes/packet overhead
- BLE graceful shutdown: 10s timeout for active operations, prevents use-after-free on restart
- Bounded BLE cache: LRU eviction with connected device protection
- Comprehensive CONCURRENCY.md with mutex ordering, timing rationale, volatile usage

**Stats:**

- 55 files modified (7,844 insertions, 174 deletions)
- 2 phases, 13 plans
- 60 commits
- Same-day execution (2026-01-24)

**Git range:** `82be835` → `51305b2`

**What's next:** Production testing, then v1.3 for new features or additional optimizations

---

## v1.1 Stability Quick Wins (Shipped: 2026-01-24)

**Delivered:** Fixed all 5 P1 stability issues from v1 audit for reliable extended firmware operation.

**Phases completed:** 6 (2 plans total)

**Key accomplishments:**

- Fixed ODR violation in Persistence module (migrated to ArduinoJson 7 API)
- Pre-allocated ResourceData vectors (256 slots) preventing heap fragmentation
- Enabled Task Watchdog Timer with 10s timeout for hang detection
- Improved LXStamper yield frequency 10x for UI responsiveness during stamping
- Added mutex protection to BLE callback queues preventing race conditions

**Stats:**

- 10 files modified (453 insertions, 42 deletions)
- 1 phase, 2 plans, 6 tasks
- Same-day execution (~13 minutes)

**Git range:** `f0fb790` → `21a04d1`

**What's next:** Runtime testing on hardware, then P2 issues or new features

---

## v1.0 Stability Audit (Shipped: 2026-01-24)

**Delivered:** Comprehensive stability audit of microReticulum firmware with prioritized backlog of 30 issues.

**Phases completed:** 1-5 (15 plans total)

**Key accomplishments:**

- Created MemoryMonitor module for runtime heap/stack monitoring with 30s periodic logging
- Created BootProfiler module with millisecond timing and SPIFFS persistence
- Documented 40+ memory pools (~550KB static, ~330KB PSRAM) with overflow behaviors
- Audited all threading patterns across LVGL, NimBLE, and FreeRTOS subsystems
- Consolidated 30 stability issues into WSJF-prioritized BACKLOG.md

**Stats:**

- 795 lines of instrumentation code (C++)
- 55 planning artifacts
- 5 phases, 15 plans
- ~2 days from project init to ship (2026-01-23 to 2026-01-24)

**Git range:** `docs(01-01)` -> `docs(05): complete Synthesis phase`

**What's next:** Implementation sprints starting with P1 issues (TWDT, thread-safe BLE queues, LXStamper yield frequency)

---
