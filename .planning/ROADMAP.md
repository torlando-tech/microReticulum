# Roadmap: microReticulum Stability Audit

## Overview

This roadmap guides a systematic stability audit of the microReticulum firmware. Starting with instrumentation to capture baseline data, we progress through boot optimization, memory allocation analysis, concurrency auditing, and conclude with a prioritized backlog of findings. Each phase delivers measurable progress toward reliable extended operation.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

Decimal phases appear between their surrounding integers in numeric order.

- [x] **Phase 1: Memory Instrumentation** - Establish heap/stack monitoring and baseline measurements
- [x] **Phase 2: Boot Profiling** - Profile boot sequence and apply configuration quick wins
- [ ] **Phase 3: Memory Allocation Audit** - Audit allocation patterns for fragmentation sources
- [ ] **Phase 4: Concurrency Audit** - Audit threading, mutex usage, and NimBLE lifecycle
- [ ] **Phase 5: Synthesis** - Consolidate findings into prioritized backlog

## Phase Details

### Phase 1: Memory Instrumentation
**Goal**: Runtime memory monitoring is operational and capturing baseline fragmentation data
**Depends on**: Nothing (first phase)
**Requirements**: MEM-01, MEM-02, DLVR-02
**Success Criteria** (what must be TRUE):
  1. Free heap and largest free block are logged periodically (at least every 30 seconds)
  2. Fragmentation percentage is calculated and logged (100 - largest_block/total_free)
  3. Stack high water marks are logged for all FreeRTOS tasks
  4. Instrumentation code is isolated and can be disabled via build flag
**Plans**: 2 plans

Plans:
- [x] 01-01-PLAN.md - Core Memory Monitor Module (heap/stack monitoring, FreeRTOS timer)
- [x] 01-02-PLAN.md - Application Integration (build flag, init in main.cpp, task registration)

### Phase 2: Boot Profiling
**Goal**: Boot sequence is profiled and reduced to under 5 seconds through configuration changes
**Depends on**: Phase 1 (instrumentation helps validate boot timing)
**Requirements**: BOOT-01, BOOT-02, BOOT-03, BOOT-04, BOOT-05, DLVR-03
**Success Criteria** (what must be TRUE):
  1. Each boot phase is timed with millisecond precision (esp_timer instrumentation)
  2. PSRAM memory test configuration is documented and optimized
  3. Flash mode (QIO/QOUT, speed) is documented and verified optimal
  4. Log level during boot is reduced to WARNING or ERROR
  5. Blocking operations in setup() are identified and documented
**Plans**: 4 plans

Plans:
- [x] 02-01-PLAN.md - BootProfiler Core Module (timing API with build flag isolation)
- [x] 02-02-PLAN.md - Boot Sequence Instrumentation (instrument setup() with timing calls)
- [x] 02-03-PLAN.md - Configuration Optimizations (PSRAM test, log level, documentation)
- [x] 02-04-PLAN.md - Validation and Persistence (SPIFFS storage, validate 5-second target)

### Phase 3: Memory Allocation Audit
**Goal**: All significant memory allocation patterns are documented with fragmentation risk assessment
**Depends on**: Phase 1 (baseline data informs audit priorities)
**Requirements**: MEM-03, MEM-04, MEM-05, MEM-06, MEM-07
**Success Criteria** (what must be TRUE):
  1. All shared_ptr creation sites are audited (make_shared vs new/shared_ptr pattern)
  2. Packet and Bytes allocation frequency and sizes are documented
  3. ArduinoJson document types (Dynamic vs Static) are audited across codebase
  4. Large buffer allocations are verified to use PSRAM (MALLOC_CAP_SPIRAM)
  5. Memory pools are documented with capacity limits and overflow behavior
**Plans**: TBD

Plans:
- [ ] 03-01: TBD

### Phase 4: Concurrency Audit
**Goal**: All threading patterns are documented with risk assessment for deadlock, race conditions, and leaks
**Depends on**: Phase 1 (stack monitoring helps identify overflow risks)
**Requirements**: CONC-01, CONC-02, CONC-03, CONC-04, CONC-05
**Success Criteria** (what must be TRUE):
  1. All LVGL API calls are audited for mutex protection
  2. NimBLE init/deinit lifecycle is documented (confirm single init, no restart cycles)
  3. All FreeRTOS tasks are verified to yield/feed watchdog regularly
  4. Mutex acquisition order is documented (deadlock potential assessment)
  5. Task stack sizes are documented with high water mark data from Phase 1
**Plans**: TBD

Plans:
- [ ] 04-01: TBD

### Phase 5: Synthesis
**Goal**: Prioritized backlog of issues with severity ratings and fix recommendations
**Depends on**: Phases 1-4 (all audit data collected)
**Requirements**: DLVR-01
**Success Criteria** (what must be TRUE):
  1. All findings from Phases 1-4 are consolidated into single backlog
  2. Each issue has severity rating (Critical/High/Medium/Low)
  3. Each issue has fix recommendation with estimated complexity
  4. Backlog is ordered by severity and fix-effort ratio
**Plans**: TBD

Plans:
- [ ] 05-01: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 1 -> 2 -> 3 -> 4 -> 5

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Memory Instrumentation | 2/2 | ✓ Complete | 2026-01-23 |
| 2. Boot Profiling | 4/4 | ✓ Complete | 2026-01-24 |
| 3. Memory Allocation Audit | 0/TBD | Not started | - |
| 4. Concurrency Audit | 0/TBD | Not started | - |
| 5. Synthesis | 0/TBD | Not started | - |

---
*Roadmap created: 2026-01-23*
*Depth: comprehensive*
*Requirements coverage: 20/20*
