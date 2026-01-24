---
phase: 03-memory-allocation-audit
plan: 02
subsystem: memory
tags: [shared_ptr, pimpl, psram, heap-fragmentation, pools]

# Dependency graph
requires:
  - phase: 03-01
    provides: Bytes.cpp analysis and MEM-01 documentation
provides:
  - MEM-03 shared_ptr pattern analysis complete
  - MEM-06 PSRAM verification continued
  - Session object allocation frequency classification
  - Fixed pool inventory for Link, Channel, Resource
affects: [03-03, 03-04, 05-memory-optimization]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Fixed-size pools for zero heap fragmentation"
    - "PSRAM via heap_caps_aligned_alloc for large pools"
    - "Pimpl idiom with shared_ptr for session objects"

key-files:
  created:
    - .planning/phases/03-memory-allocation-audit/03-02-FINDINGS.md
  modified: []

key-decisions:
  - "new/shared_ptr pattern acceptable for startup allocations"
  - "make_shared preferred for future optimization but not blocking"
  - "Resource vectors identified as fragmentation risk during transfers"

patterns-established:
  - "Pool overflow: cull by timestamp for caches, reject for fixed buffers"
  - "Allocation frequency categories: startup, per-link, per-transfer, per-packet"

# Metrics
duration: 18min
completed: 2026-01-24
---

# Phase 3 Plan 02: shared_ptr and Session Object Audit Summary

**Documented 14 new/shared_ptr allocation sites vs 2 make_shared sites across 7 pimpl session classes; verified Identity PSRAM pool allocation; classified allocation frequency as startup/per-link/per-transfer**

## Performance

- **Duration:** 18 min
- **Started:** 2026-01-24T13:59:11Z
- **Completed:** 2026-01-24T14:17:00Z
- **Tasks:** 3 (audit tasks combined into findings document)
- **Files modified:** 0 (audit-only phase)
- **Files created:** 1

## Accomplishments

- Audited 7 core session classes: Identity, Destination, Link, Channel, Resource, Buffer
- Documented pattern distribution: 14 `new T()` + `shared_ptr` vs 2 `make_shared`
- Verified Identity pools use PSRAM via `heap_caps_aligned_alloc` (MEM-06)
- Catalogued fixed-size pools: destinations (192), ratchets (128), resources (8+8), requests (8), ring buffers (16+16)
- Classified allocation frequency: startup-only, per-link-establishment, per-transfer
- Identified Resource vectors as potential fragmentation source during large transfers

## Task Commits

This was an audit-only plan. Tasks 1-2 (audit work) produced Task 3 (findings document):

1. **Task 1-3: Identity/Destination/Link/Channel/Resource audit and findings** - `ff798d5` (docs)

**Plan metadata:** To be committed

## Files Created

- `.planning/phases/03-memory-allocation-audit/03-02-FINDINGS.md` - Comprehensive shared_ptr pattern analysis with recommendations for Phase 5

## Decisions Made

- **new/shared_ptr pattern acceptable for now:** Most allocations occur at startup or per-link-establishment, not per-packet. The overhead (~40 bytes per object) is tolerable.
- **Resource vectors need attention:** `_parts` and `_hashmap` vectors in ResourceData resize during transfers, potentially causing fragmentation for large file transfers.
- **Fixed pools working well:** Link, Channel pools already use fixed-size arrays eliminating fragmentation.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None - audit proceeded smoothly. All target files were accessible and patterns clearly documented.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

**Ready for Plan 03-03 (Interface/Packet allocation patterns):**
- MEM-03 complete, provides baseline for comparing interface allocation patterns
- Understanding of pimpl pattern established
- Can now compare dynamic vs static allocation approaches

**Ready for Plan 03-04 (Large object audit):**
- Resource vectors flagged for attention
- Pool sizes documented
- PSRAM verification pattern established

**Concerns:**
- Resource vector fragmentation during large transfers warrants Phase 5 optimization
- 14 sites could use make_shared but not urgent

---
*Phase: 03-memory-allocation-audit*
*Plan: 02*
*Completed: 2026-01-24*
