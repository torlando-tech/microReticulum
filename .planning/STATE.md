# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-01-23)

**Core value:** Identify and prioritize root causes of instability for reliable extended operation
**Current focus:** Phase 4 COMPLETE - Ready for Phase 5 (Stability Fixes)

## Current Position

Phase: 4 of 5 COMPLETE (Concurrency Audit)
Plan: 04 of 4 COMPLETE
Status: Phase 4 complete, all concurrency audits finished
Last activity: 2026-01-24 — Completed 04-04 (Concurrency Synthesis)

Progress: [##########] 100% (Phase 4)

## Performance Metrics

**Velocity:**
- Total plans completed: 13
- Average duration: 5 min
- Total execution time: 62 min

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 01-memory-instrumentation | 2 | 4min | 2min |
| 02-boot-profiling | 4 | 8min | 2min |
| 03-memory-allocation-audit | 4 | 26min | 6.5min |
| 04-concurrency-audit | 4 | 24min | 6min |

**Recent Trend:**
- Last 5 plans: 03-04 (5min), 04-01 (12min), 04-02 (4min), 04-03 (4min), 04-04 (4min)
- Phase 4 complete in 24min across 4 plans

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
- [04-04]: 4-level mutex hierarchy: LVGL (outermost) -> BLE Interface -> NimBLE Platform -> Spinlocks (innermost)
- [04-04]: Implicit mutex ordering is safe - no cross-subsystem nesting detected

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

### Concurrency Audit Findings (COMPLETE)

**Phase 4 Summary:**
- **Total Issues:** 18 (0 Critical, 4 High, 9 Medium, 5 Low)
- **Mutexes Documented:** 5 primitives across 3 subsystems
- **Deadlock Risk:** LOW (no cross-subsystem mutex nesting)
- **Phase 4 Reports:**
  - 04-LVGL.md: LVGL thread safety audit
  - 04-NIMBLE.md: NimBLE lifecycle audit
  - 04-TASKS.md: FreeRTOS tasks and watchdog audit
  - 04-MUTEX.md: Mutex ordering and deadlock analysis
  - 04-SUMMARY.md: Consolidated findings

**04-01: LVGL Thread Safety Audit**
- 64 LVGL_LOCK() call sites across 10 UI files
- 36 event callbacks - all run in LVGL task context (safe)
- 3 Medium severity issues (missing LVGL_LOCK in constructors/destructors)
- Audit report: .planning/phases/04-concurrency-audit/04-LVGL.md

**04-02: NimBLE Lifecycle Audit**
- 7 issues identified (0 Critical, 2 High, 3 Medium, 2 Low)
- NIMBLE-01: Pending queues not thread-safe (High)
- NIMBLE-02: Shutdown during active operations (High)
- 50+ spinlock sites for atomic state transitions
- Audit report: .planning/phases/04-concurrency-audit/04-NIMBLE.md

**04-03: FreeRTOS Tasks and Watchdog Audit**
- 3 FreeRTOS tasks inventoried (LVGL, BLE, main loop) + 1 timer
- TWDT (Task Watchdog Timer) NOT configured - critical gap
- 5 issues identified (0 critical, 2 high, 2 medium, 1 low)
- Audit report: .planning/phases/04-concurrency-audit/04-TASKS.md

**04-04: Mutex Ordering and Deadlock Analysis**
- 5 synchronization primitives documented
- 4-level mutex hierarchy established
- No cross-subsystem nesting detected (deferred work pattern)
- 2 issues (1 Medium, 1 Low)
- Audit report: .planning/phases/04-concurrency-audit/04-MUTEX.md

### Phase 5 Consolidated Backlog

**From Memory Audit (Phase 3):**
- P2-1: Packet Object Pool (High impact)
- P2-2: Inline Small Bytes Members (Medium-High impact)
- P2-3: Resource Vector Pre-allocation (Medium impact)
- P3-1: Convert new/shared_ptr to make_shared
- P3-2: ArduinoJson migration
- P4-1: Bytes Arena Allocator
- P4-2: BLEFragmenter Output Parameter
- P4-3: String Reserve in toHex

**From Concurrency Audit (Phase 4):**

Priority 1 (Critical):
- P1-1: Enable TWDT with 10s timeout, subscribe critical tasks (High)

Priority 2 (High):
- P2-4: Add thread-safe access to BLE pending queues (NIMBLE-01)
- P2-5: Add graceful shutdown with operation wait (NIMBLE-02)
- P2-6: Improve LXStamper yield frequency (TASK-02)

Priority 3 (Medium):
- P3-3: Add LVGL_LOCK() to 3 screen constructors/destructors
- P3-4: Add timeout to LVGL mutex acquisition
- P3-5: Add timeout to I2S audio writes
- P3-6: Add logging for connection mutex timeout
- P3-7: Check connection status before cache eviction
- P3-8: Consider hard recovery with deinit/init cycle

Priority 4 (Low):
- P4-4: Implement Link watchdog
- P4-5: Document 50ms recovery delay rationale
- P4-6: Add debug-mode mutex ordering assertions
- P4-7: Document volatile usage in native GAP handler

### Pending Todos

None - Phase 4 complete.

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
- BLE pending queues lack thread-safe access

## Session Continuity

Last session: 2026-01-24
Completed: 04-04-PLAN.md (Concurrency Synthesis)
Next: Phase 5 (Stability Fixes)

---
*Phase 4 (Concurrency Audit) COMPLETE.*
*Consolidated report: .planning/phases/04-concurrency-audit/04-SUMMARY.md*
*Ready for Phase 5 implementation.*
