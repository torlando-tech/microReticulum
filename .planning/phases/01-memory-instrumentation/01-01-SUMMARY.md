---
phase: 01-memory-instrumentation
plan: 01
subsystem: instrumentation
tags: [esp32, freertos, heap-caps, memory-monitoring, psram]

# Dependency graph
requires: []
provides:
  - MemoryMonitor class with heap/stack monitoring API
  - Build flag isolation pattern (MEMORY_INSTRUMENTATION_ENABLED)
  - FreeRTOS timer-based periodic logging
  - Task registry for stack high water mark tracking
affects: [02-boot-audit, 03-memory-audit, stability-monitoring]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Static-only allocation in instrumentation (no heap usage)
    - FreeRTOS software timer for periodic callbacks
    - Explicit task handle registry (workaround for Arduino framework limitations)

key-files:
  created:
    - src/Instrumentation/MemoryMonitor.h
    - src/Instrumentation/MemoryMonitor.cpp
  modified: []

key-decisions:
  - "Use RNS::Instrumentation namespace for new instrumentation code"
  - "Static buffers (256 bytes each) for log formatting to avoid stack pressure"
  - "Warn at 50% fragmentation threshold and 256 bytes stack remaining"

patterns-established:
  - "Build flag guard pattern: #ifdef MEMORY_INSTRUMENTATION_ENABLED with stub macros when disabled"
  - "Task registry pattern: static array with explicit register/unregister calls"
  - "Log prefix convention: [HEAP] for heap stats, [STACK] for stack stats, [MEM_MON] for lifecycle"

# Metrics
duration: 2min
completed: 2026-01-24
---

# Phase 1 Plan 01: MemoryMonitor Core Module Summary

**ESP32-S3 heap/stack monitoring via heap_caps APIs and FreeRTOS timer with zero dynamic allocation**

## Performance

- **Duration:** 2 min
- **Started:** 2026-01-24T03:58:22Z
- **Completed:** 2026-01-24T04:00:13Z
- **Tasks:** 2
- **Files created:** 2

## Accomplishments
- Complete MemoryMonitor API with init/stop/registerTask/unregisterTask/setVerbose/logNow
- Heap monitoring for both internal RAM and PSRAM (free, largest block, min free, fragmentation %)
- Task stack high water mark tracking with explicit registry (up to 16 tasks)
- Build flag isolation with stub macros for zero overhead when disabled

## Task Commits

Each task was committed atomically:

1. **Task 1: Create MemoryMonitor header** - `7374a70` (feat)
2. **Task 2: Implement MemoryMonitor with ESP-IDF APIs** - `b213bda` (feat)

## Files Created

- `src/Instrumentation/MemoryMonitor.h` - Public API with class declaration and stub macros
- `src/Instrumentation/MemoryMonitor.cpp` - Implementation using heap_caps and FreeRTOS APIs

## Decisions Made

1. **RNS::Instrumentation namespace** - New namespace for instrumentation code, keeping it separate from core RNS functionality
2. **256-byte static buffers** - Sized to handle typical log output without risking stack overflow in timer callback
3. **50% fragmentation warning threshold** - Based on ESP-IDF documentation guidance that fragmentation >50% is problematic
4. **256-byte stack warning threshold** - Conservative threshold to catch potential stack overflow before crash

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required. To enable, add `-DMEMORY_INSTRUMENTATION_ENABLED` to build flags.

## Next Phase Readiness

- MemoryMonitor module ready for integration
- Next steps: Create LogSession for SD card file output (01-02-PLAN)
- Integration with main application will require:
  - Calling `MemoryMonitor::init()` at startup
  - Registering key tasks with `registerTask()`
  - Adding build flag to platformio.ini

---
*Phase: 01-memory-instrumentation*
*Completed: 2026-01-24*
