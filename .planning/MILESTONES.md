# Project Milestones: microReticulum

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
