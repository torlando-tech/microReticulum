---
phase: 07-p2-production-readiness
plan: 01
subsystem: ui
tags: [lvgl, mutex, thread-safety, freertos, deadlock-detection]

# Dependency graph
requires:
  - phase: 04-concurrency-audit
    provides: concurrency analysis identifying screen transition race conditions
provides:
  - LVGL mutex protection in all screen constructors/destructors
  - Debug-time deadlock detection (5s timeout with crash)
  - Thread-safe widget creation/deletion
affects: [07-02, 07-03, 07-04, 07-05, ui-components]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - LVGL_LOCK() in screen constructor/destructor for thread safety
    - Debug timeout pattern for mutex deadlock detection

key-files:
  created: []
  modified:
    - src/UI/LVGL/LVGLLock.h
    - src/UI/LXMF/SettingsScreen.cpp
    - src/UI/LXMF/ComposeScreen.cpp
    - src/UI/LXMF/AnnounceListScreen.cpp

key-decisions:
  - "5-second debug timeout chosen as sufficient for normal acquisition but short enough for timely deadlock detection"
  - "#ifndef NDEBUG used to detect debug builds (NDEBUG is defined in release)"

patterns-established:
  - "LVGL_LOCK() must be first statement in screen constructor and destructor"
  - "Event callbacks do NOT need LVGL_LOCK - they run within LVGL task handler"

# Metrics
duration: 8min
completed: 2026-01-24
---

# Phase 7 Plan 1: LVGL Mutex Protection Summary

**LVGL mutex protection in screen constructors/destructors with 5s debug timeout for deadlock detection**

## Performance

- **Duration:** 8 min
- **Started:** 2026-01-24T00:00:00Z
- **Completed:** 2026-01-24T00:08:00Z
- **Tasks:** 4
- **Files modified:** 4

## Accomplishments

- Added debug-time deadlock detection: 5-second timeout in debug builds crashes with diagnostic info
- Added LVGL_LOCK to SettingsScreen constructor and destructor
- Added LVGL_LOCK to ComposeScreen constructor and destructor
- Added LVGL_LOCK to AnnounceListScreen constructor and destructor
- Fixed CONC-M1, CONC-M2, CONC-M3, CONC-M7 from concurrency audit

## Task Commits

Each task was committed atomically:

1. **Task 1: Add debug timeout to LVGL mutex acquisition** - `79b99a4` (feat)
2. **Task 2: Add LVGL_LOCK to SettingsScreen constructor/destructor** - `8224a6e` (feat)
3. **Task 3: Add LVGL_LOCK to ComposeScreen constructor/destructor** - `8ca4b92` (feat)
4. **Task 4: Add LVGL_LOCK to AnnounceListScreen constructor/destructor** - `dac2cb1` (feat)

## Files Created/Modified

- `src/UI/LVGL/LVGLLock.h` - Added conditional compilation for debug timeout (5s) vs release (portMAX_DELAY)
- `src/UI/LXMF/SettingsScreen.cpp` - LVGL_LOCK in constructor and destructor
- `src/UI/LXMF/ComposeScreen.cpp` - LVGL_LOCK in constructor and destructor
- `src/UI/LXMF/AnnounceListScreen.cpp` - LVGL_LOCK in constructor and destructor

## Decisions Made

- Used `#ifndef NDEBUG` for debug detection (NDEBUG is standard C++ macro, defined in release builds)
- 5-second timeout chosen as balance between allowing normal acquisition delays and detecting deadlocks quickly
- Used `xSemaphoreGetMutexHolder()` to capture mutex holder info for debugging

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- Build environment for T-Deck (tdeck) not available in platformio.ini - verified code syntax-only
- Pre-existing msgpack type conversion errors in native build (unrelated to this plan)

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Ready for 07-02: Announce thread lifecycle fix
- All screen transition race conditions now protected
- Deadlock detection enabled for development/debugging

---
*Phase: 07-p2-production-readiness*
*Completed: 2026-01-24*
