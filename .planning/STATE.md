# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-01-23)

**Core value:** Identify and prioritize root causes of instability for reliable extended operation
**Current focus:** Phase 4 (Concurrency Audit) - Plan 03 complete

## Current Position

Phase: 4 of 5 IN PROGRESS (Concurrency Audit)
Plan: 03 of 4 COMPLETE
Status: FreeRTOS task audit complete, ready for Synthesis
Last activity: 2026-01-24 — Completed 04-03 (FreeRTOS Tasks and Watchdog Audit)

Progress: [#########-] 88%

## Performance Metrics

**Velocity:**
- Total plans completed: 12
- Average duration: 5 min
- Total execution time: 58 min

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 01-memory-instrumentation | 2 | 4min | 2min |
| 02-boot-profiling | 4 | 8min | 2min |
| 03-memory-allocation-audit | 4 | 26min | 6.5min |
| 04-concurrency-audit | 3 | 20min | 7min |

**Recent Trend:**
- Last 5 plans: 03-03 (3min), 03-04 (5min), 04-01 (12min), 04-02 (4min), 04-03 (4min)
- Trend: Concurrency audit averaging 7min/plan

*Updated after each plan completion*

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [Init]: Data-driven approach - instrument first, then audit based on findings
- [Init]: Boot and memory audits run in parallel after instrumentation
- [01-01]: RNS::Instrumentation namespace for new instrumentation code
- [01-01]: Static 256-byte buffers for log formatting (avoid stack pressure)
- [01-01]: Warn at 50% fragmentation threshold, 256 bytes stack remaining
- [01-02]: Initialize memory monitor after LVGL task starts (before Reticulum)
- [01-02]: 30-second monitoring interval per CONTEXT.md
- [02-01]: Use millis() for boot timing (sufficient precision, already in codebase)
- [02-01]: First markStart() establishes boot start time (no explicit init)
- [02-01]: Wait time tracked separately from init time
- [02-02]: Wrap setup_*() calls in main setup() rather than inside each function
- [02-02]: WAIT markers inside setup_wifi() and setup_lxmf() where waits occur
- [02-03]: Disable PSRAM memory test for ~2s boot time savings
- [02-04]: Enable BOOT_REDUCED_LOGGING with CORE_DEBUG_LEVEL=2
- [02-04]: 5s target not achievable via config - reticulum init is 2.5s
- [03-02]: new/shared_ptr pattern acceptable for startup allocations
- [03-02]: Resource vectors identified as fragmentation risk during transfers
- [03-03]: DynamicJsonDocument in Persistence requires migration to JsonDocument
- [03-04]: 40+ memory pools documented, all with overflow protection
- [04-01]: LVGL event callbacks run in LVGL task context (no explicit lock needed)
- [04-01]: Screen constructors/destructors should have LVGL_LOCK() for defensive coding
- [04-01]: Recursive mutex correctly handles nested LVGL calls

### Boot Profiling Findings

**Final boot timing (with all optimizations):**
- Total: 9,704ms
- Init: 5,336ms (336ms over 5s target)
- Wait: 4,368ms (45% of boot)

**Longest phases (init time):**
1. reticulum: 2,580ms (cryptographic operations)
2. ui_manager: 957ms
3. hardware: 509ms
4. lvgl: 482ms
5. gps: 553ms (excluding 368ms GPS sync wait)

**Blocking operations identified:**
- TCP stabilization: 3,000ms fixed delay
- WiFi connect: ~1,000ms (30s timeout)
- GPS sync: ~370-950ms variable

### Memory Allocation Audit Findings (COMPLETE)

**Final Summary:**
- **Total Issues:** 13 (0 Critical, 5 High, 4 Medium, 4 Low)
- **Memory Pools:** 40+ documented
- **Total Pool Memory:** ~550KB (excluding LVGL)
- **PSRAM Usage:** ~330KB (Identity + LVGL)

**03-01: Core Data Path (Bytes, Packet, Transport)**
- PSRAM verified: Bytes uses PSRAMAllocator for all vector storage
- Transport well-optimized: 20+ fixed-size pools (~21KB static)
- Packet is main concern: 10+ allocations per packet (Object + 9 Bytes)

**03-02: shared_ptr and Session Objects**
- 14 sites use `new T()` + `shared_ptr` pattern (2 allocations)
- 2 sites use `make_shared` (1 allocation) - Buffer.cpp, BLEPlatform.cpp
- Identity pools use PSRAM via heap_caps_aligned_alloc (verified)
- Fixed pools in Link/Channel eliminate fragmentation
- Resource vectors resize during transfers - fragmentation risk

**03-03: ArduinoJson and Persistence**
- ArduinoJson 7.4.2 configured in platformio.ini
- 1 deprecated pattern: DynamicJsonDocument in Persistence.h/cpp (Medium)
- MessageStore uses correct JsonDocument pattern with reuse
- UI: 8 screen objects at startup (correct pattern)
- BLE: All pool-based with fixed sizes
- LVGL buffers correctly in PSRAM (307KB)

**03-04: Memory Pools Documentation**
- 40+ pools documented with sizes and overflow behaviors
- Complete audit report: .planning/phases/03-memory-allocation-audit/03-AUDIT.md
- Reference copy: docs/MEMORY_AUDIT.md
- Phase 5 backlog created with prioritized fixes

**Complete Pool Inventory:**
| Category | Pools | Size | PSRAM |
|----------|-------|------|-------|
| Transport | 20+ | ~21KB | No |
| Identity | 2 | ~30KB | Partial |
| MessageStore | 2 | ~288KB | Should consider |
| Link/Channel | 5 | ~5KB/link | No |
| BLE | 8 | ~200KB | Should consider |
| LXMF | 10+ | Variable | No |

### Concurrency Audit Findings (IN PROGRESS)

**04-01: LVGL Thread Safety Audit (COMPLETE)**
- 64 LVGL_LOCK() call sites across 10 UI files
- 36 event callbacks - all run in LVGL task context (safe)
- 3 Medium severity issues identified:
  - SettingsScreen: Constructor/destructor missing LVGL_LOCK()
  - ComposeScreen: Constructor/destructor missing LVGL_LOCK()
  - AnnounceListScreen: Constructor/destructor missing LVGL_LOCK()
- Threading model documented with ASCII diagrams
- Audit report: .planning/phases/04-concurrency-audit/04-LVGL.md

**04-03: FreeRTOS Tasks and Watchdog Audit (COMPLETE)**
- 3 FreeRTOS tasks inventoried (LVGL, BLE, main loop) + 1 timer
- TWDT (Task Watchdog Timer) NOT configured - critical gap
- 5 issues identified (0 critical, 2 high, 2 medium, 1 low):
  - TASK-01: TWDT not configured (High)
  - TASK-02: LXStamper CPU hogging with infrequent yields (High)
  - TASK-03: LVGL mutex uses portMAX_DELAY (Medium)
  - TASK-04: Audio I2S blocking write (Medium)
  - TASK-05: Link watchdog TODO not implemented (Low)
- Yield patterns and blocking operations documented with risk assessment
- Stack sizes assessed against Phase 1 monitoring
- Audit report: .planning/phases/04-concurrency-audit/04-TASKS.md

### Phase 5 Backlog (from 03-AUDIT.md)

**Priority 2 (High):**
- P2-1: Packet Object Pool (High impact)
- P2-2: Inline Small Bytes Members (Medium-High impact)
- P2-3: Resource Vector Pre-allocation (Medium impact)

**Priority 3 (Medium):**
- P3-1: Convert new/shared_ptr to make_shared
- P3-2: ArduinoJson migration

**Priority 4 (Low):**
- P4-1: Bytes Arena Allocator
- P4-2: BLEFragmenter Output Parameter
- P4-3: String Reserve in toHex

**From 04-LVGL.md:**
- P3-3: Add LVGL_LOCK() to 3 screen constructors/destructors (Medium)

**From 04-TASKS.md:**
- P1-1: Enable TWDT with 10s timeout, subscribe critical tasks (High)
- P2-4: Improve LXStamper yield frequency (High)
- P3-4: Add timeout to LVGL mutex acquisition (Medium)
- P3-5: Add timeout to I2S audio writes (Medium)
- P4-4: Implement Link watchdog (Low)

### Pending Todos

None - Phase 4 Plan 03 complete.

### Blockers/Concerns

**Resolved:**
- `PSRAMAllocator.h` - Created in src/
- `partitions.csv` - Created in examples/lxmf_tdeck/

**Identified for Phase 5:**
- Resource vectors (`_parts`, `_hashmap`) resize during transfers
- 14 sites could use `make_shared` for ~40 bytes savings each
- DynamicJsonDocument in Persistence.h/cpp needs migration
- 3 screen classes need LVGL_LOCK() in constructors/destructors
- TWDT not configured for any application tasks
- LXStamper yields only every 100 rounds during stamp generation

## Session Continuity

Last session: 2026-01-24
Completed: 04-03-PLAN.md (FreeRTOS Tasks and Watchdog Audit)
Next: 04-04-PLAN.md (Concurrency Synthesis)

---
*Phase 4 (Concurrency Audit) Plan 03 COMPLETE.*
*Task audit report: .planning/phases/04-concurrency-audit/04-TASKS.md*
