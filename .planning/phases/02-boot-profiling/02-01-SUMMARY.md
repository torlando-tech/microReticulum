---
phase: 02-boot-profiling
plan: 01
subsystem: instrumentation
tags: [boot, timing, profiling, esp32, millis]

# Dependency graph
requires:
  - phase: 01-memory-instrumentation
    provides: Instrumentation namespace and static buffer pattern
provides:
  - BootProfiler module for timing boot phases
  - Build flag isolation (BOOT_PROFILING_ENABLED)
  - Init vs wait time distinction
affects: [02-02-integration, 02-03-optimization]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Static class with static buffers for log formatting
    - Build flag guards with stub macros when disabled

key-files:
  created:
    - src/Instrumentation/BootProfiler.h
    - src/Instrumentation/BootProfiler.cpp
  modified: []

key-decisions:
  - "Use millis() for timing (sufficient precision, already in codebase)"
  - "First markStart() establishes boot start time (no explicit init call needed)"
  - "Wait time tracked separately from init time for meaningful breakdown"

patterns-established:
  - "BOOT_PROFILE_START/END macros for phase timing"
  - "BOOT_PROFILE_WAIT_START/END for blocking operations"
  - "[BOOT] log prefix for all boot profiler output"

# Metrics
duration: 1min
completed: 2026-01-24
---

# Phase 2 Plan 01: Boot Profiler Core Summary

**BootProfiler module with millisecond timing, phase/wait separation, and build flag isolation following MemoryMonitor patterns**

## Performance

- **Duration:** 1 min
- **Started:** 2026-01-24T04:45:29Z
- **Completed:** 2026-01-24T04:46:50Z
- **Tasks:** 2
- **Files created:** 2

## Accomplishments
- Created BootProfiler header with public API and stub macros
- Implemented timing logic using millis() with static buffers
- Distinguishes init time from blocking wait time
- Logs cumulative time at each phase end for boot analysis

## Task Commits

Each task was committed atomically:

1. **Task 1: Create BootProfiler header with API** - `6fc457f` (feat)
2. **Task 2: Implement BootProfiler timing logic** - `ada861f` (feat)

## Files Created

- `src/Instrumentation/BootProfiler.h` - Public API with build flag guards and stub macros
- `src/Instrumentation/BootProfiler.cpp` - Implementation using millis() for timing

## API Overview

```cpp
// Phase timing
BOOT_PROFILE_START("WiFi");
BOOT_PROFILE_END("WiFi");

// Blocking wait timing (tracked separately)
BOOT_PROFILE_WAIT_START("WiFi connect");
BOOT_PROFILE_WAIT_END("WiFi connect");

// Final summary
BOOT_PROFILE_COMPLETE();
```

Example output:
```
[BOOT] Profiling started
[BOOT] START: WiFi (at 0ms)
[BOOT] WAIT: WiFi connect (2150ms, total wait: 2150ms)
[BOOT] END: WiFi (2180ms, cumulative: 2180ms)
[BOOT] COMPLETE: total=5000ms, init=2850ms, wait=2150ms
[BOOT] Wait time: 43% of boot
```

## Decisions Made

- **millis() for timing:** Sufficient millisecond precision, already used in codebase, simpler than esp_timer_get_time
- **No explicit init call:** First markStart() auto-initializes boot start time
- **Separate wait tracking:** Allows meaningful analysis of where time goes (CPU work vs I/O blocking)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- BootProfiler module ready for integration in T-Deck firmware
- Next plan (02-02) will add instrumentation calls throughout boot sequence
- Build flag BOOT_PROFILING_ENABLED needed in platformio.ini

---
*Phase: 02-boot-profiling*
*Plan: 01*
*Completed: 2026-01-24*
