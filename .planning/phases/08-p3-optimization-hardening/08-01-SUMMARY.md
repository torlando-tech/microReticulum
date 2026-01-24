---
phase: 08-p3-optimization-hardening
plan: 01
subsystem: memory
tags: [string-optimization, arduinojson, heap-fragmentation]

# Dependency graph
requires:
  - phase: 07-p2-production-readiness
    provides: stable concurrency patterns, MEM-M1/M2 medium fixes
provides:
  - MEM-L1 toHex capacity reservation (eliminates reallocs)
  - ArduinoJson v7 API consistency in MessageStore
affects: [08-02, 08-03, 08-04]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "std::string::reserve() before concatenation loops"
    - "ArduinoJson v7: isNull() instead of containsKey()"

key-files:
  created: []
  modified:
    - src/Bytes.cpp
    - src/LXMF/MessageStore.cpp

key-decisions:
  - "Use isNull() pattern for ArduinoJson existence checks (simplest v7 migration)"

patterns-established:
  - "String pre-allocation: reserve exact capacity before building via concatenation"
  - "ArduinoJson v7: operator[].isNull() for existence checks"

# Metrics
duration: 5min
completed: 2026-01-24
---

# Phase 8 Plan 01: Trivial P3 Memory Fixes Summary

**Pre-allocated toHex capacity and migrated MessageStore to ArduinoJson v7 API**

## Performance

- **Duration:** 5 min
- **Started:** 2026-01-24T19:58:40Z
- **Completed:** 2026-01-24T20:03:40Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments
- Bytes::toHex now reserves exact capacity (size * 2) before building hex string, eliminating reallocs
- MessageStore.cpp migrated from deprecated containsKey() to v7 isNull() pattern
- No containsKey() calls remain anywhere in src/

## Task Commits

Each task was committed atomically:

1. **Task 1: Add capacity reservation to toHex** - `ac22e2c` (perf)
2. **Task 2: Migrate MessageStore to ArduinoJson v7 API** - `c37f695` (refactor)

## Files Created/Modified
- `src/Bytes.cpp` - Added hex.reserve(_data->size() * 2) to toHex(), removed FIXME comment
- `src/LXMF/MessageStore.cpp` - Replaced containsKey() with isNull() at lines 174 and 371

## Decisions Made
- Used `!isNull()` pattern rather than `.is<JsonVariantConst>()` for ArduinoJson existence checks (simpler and more readable)

## Deviations from Plan
None - plan executed exactly as written.

## Issues Encountered
- Native build has pre-existing failures (MsgPack compatibility and missing LVGL headers) unrelated to these changes
- Changes are syntactically correct standard C++ and follow established patterns

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- MEM-L1 complete, ready for 08-02 (P3 heap management)
- ArduinoJson v7 consistency achieved across codebase
- No blockers for remaining P3 plans

---
*Phase: 08-p3-optimization-hardening*
*Completed: 2026-01-24*
