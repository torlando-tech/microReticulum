---
phase: 06-p1-stability-fixes
plan: 01
subsystem: memory
tags: [odr, json, vector, heap, esp32, arduinojson]

# Dependency graph
requires:
  - phase: 05-synthesis
    provides: WSJF-prioritized P1 issue list
provides:
  - ODR-compliant Persistence module with ArduinoJson 7 API
  - Pre-allocated ResourceData vectors preventing heap fragmentation
affects: [stability, resource-transfers, large-files]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "extern declaration in header, definition in cpp for ODR compliance"
    - "vector::reserve() at construction for known-bounded collections"

key-files:
  created: []
  modified:
    - src/Utilities/Persistence.h
    - src/Utilities/Persistence.cpp
    - src/ResourceData.h

key-decisions:
  - "Use ArduinoJson 7 JsonDocument (elastic allocation) instead of deprecated DynamicJsonDocument"
  - "MAX_PARTS = 256 based on MAX_EFFICIENT_SIZE (16384) / minimum SDU (~64)"

patterns-established:
  - "ODR compliance: extern in header, definition in cpp within namespace scope"
  - "Vector pre-allocation: reserve() in constructor for bounded collections"

# Metrics
duration: 5min
completed: 2026-01-24
---

# Phase 6 Plan 01: Memory Fixes Summary

**Fixed ODR violation in Persistence module using ArduinoJson 7 API, and pre-allocated ResourceData vectors to 256 parts preventing heap fragmentation during large file transfers**

## Performance

- **Duration:** 5 min
- **Started:** 2026-01-24T17:42:40Z
- **Completed:** 2026-01-24T17:47:30Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments

- Eliminated ODR violation in Persistence module by moving static definitions from header to cpp file
- Migrated from deprecated DynamicJsonDocument to ArduinoJson 7 JsonDocument API
- Added MAX_PARTS constant (256) to ResourceData for bounded resource transfers
- Pre-allocated _parts and _hashmap vectors at construction time

## Task Commits

Each task was committed atomically:

1. **Task 1: Fix ODR violation in Persistence module** - `6760c33` (fix) - Note: Already committed in prior session
2. **Task 2: Pre-allocate ResourceData vectors** - `cd62102` (feat)

**Plan metadata:** TBD (docs: complete plan)

## Files Created/Modified

- `src/Utilities/Persistence.h` - Changed static definitions to extern declarations, migrated to JsonDocument
- `src/Utilities/Persistence.cpp` - Added namespace-scoped single definitions
- `src/ResourceData.h` - Added MAX_PARTS constant and constructor pre-allocation

## Decisions Made

1. **ArduinoJson 7 migration:** Used `JsonDocument` instead of deprecated `DynamicJsonDocument`. JsonDocument uses elastic allocation, no size parameter needed.
2. **MAX_PARTS sizing:** Set to 256 based on MAX_EFFICIENT_SIZE (16384 bytes) / minimum SDU (~64 bytes). This covers the maximum practical number of parts in a resource transfer.

## Deviations from Plan

### Pre-existing State

**Task 1 was already completed** in a prior session (commit 6760c33). The plan was executed partially before, with the ODR fix included in a commit labeled "06-02". Task 2 (ResourceData pre-allocation) was the only remaining work.

---

**Total deviations:** 0 code deviations (plan discrepancy noted above)
**Impact on plan:** None - both fixes now verified complete and working.

## Issues Encountered

- **Native build environment has pre-existing failures:** The `pio run -e native` build fails due to MsgPack type conversion issues unrelated to this plan. Verified by stashing changes and confirming failure pre-exists. Used `tdeck-bluedroid` environment for all verification.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- MEM-01 and MEM-02 complete
- Build compiles cleanly without ODR warnings
- Ready for additional P1 fixes in subsequent plans (CONC-01 through CONC-03)

---
*Phase: 06-p1-stability-fixes*
*Completed: 2026-01-24*
