---
phase: 03-memory-allocation-audit
plan: 01
subsystem: core-data-path
tags: [memory, allocation, psram, bytes, packet, transport]
dependency-graph:
  requires: [01-memory-instrumentation]
  provides: [MEM-04-partial, MEM-06-partial, core-allocation-audit]
  affects: [03-02, 03-03, 05-memory-fixes]
tech-stack:
  added: []
  patterns: [PSRAM-allocation, COW-bytes, fixed-pools]
key-files:
  created:
    - .planning/phases/03-memory-allocation-audit/03-01-FINDINGS.md
  modified:
    - src/Bytes.cpp
    - src/Packet.cpp
    - src/Packet.h
    - src/Transport.h
decisions:
  - id: frag-severity
    choice: "4-tier: Critical/High/Medium/Low"
    reason: "Matches CONTEXT.md specification"
  - id: psram-threshold
    choice: ">1KB without PSRAM is Critical"
    reason: "ESP32-S3 internal RAM constraints"
metrics:
  duration: 3min
  completed: 2026-01-24
---

# Phase 3 Plan 01: Core Data Path Audit Summary

**One-liner:** Bytes uses PSRAM correctly, Transport has fixed pools, Packet per-object allocation is main fragmentation risk (5 High issues)

## What Was Done

### Task 1: Audit Bytes class allocation patterns
- Documented 3 allocation sites in Bytes.cpp/h
- Verified PSRAM usage via `PSRAMAllocator<uint8_t>` typedef
- Identified COW copy allocation as High severity
- Added 3 FIXME(frag) comments

### Task 2: Audit Packet class and Transport allocations
- Documented 4 allocation sites in Packet.cpp/h
- Verified Transport uses extensive fixed-size pools (21KB static)
- Identified per-packet Object allocation as main concern (5 High issues)
- Added 4 FIXME(frag) comments + pool overflow documentation

### Task 3: Create core data path findings document
- Created comprehensive 03-01-FINDINGS.md with:
  - Severity-rated issue tables
  - Transport pool size estimates
  - PSRAM verification results
  - Phase 5 fix recommendations with complexity

## Key Findings

| Category | Count | Notes |
|----------|-------|-------|
| Critical | 0 | All large allocations use PSRAM |
| High | 5 | Per-packet allocations |
| Medium | 2 | make_shared optimizations |
| Low | 1 | String reallocation |

**PSRAM Verification:** Bytes class correctly routes all vector storage to PSRAM via PSRAMAllocator typedef on line 55 of Bytes.h.

**Transport Assessment:** Already well-optimized with 15+ fixed-size pools replacing STL containers. Total static allocation ~21KB.

**Main Concern:** Packet class creates 10+ allocations per packet (Object + 9 Bytes members), each with shared_ptr control block overhead.

## Files Changed

| File | Changes |
|------|---------|
| src/Bytes.cpp | 3 FIXME(frag) comments added |
| src/Packet.cpp | 2 FIXME(frag) comments added |
| src/Packet.h | 2 FIXME(frag) comments added |
| src/Transport.h | Pool overflow behavior documented |
| .planning/.../03-01-FINDINGS.md | 250 lines, full audit findings |

## Commits

| Hash | Message |
|------|---------|
| 2262dfb | audit(03-01): document Bytes class allocation patterns |
| 51018f4 | audit(03-01): document Packet and Transport allocation patterns |
| cd4a697 | docs(03-01): create core data path audit findings |

## Deviations from Plan

None - plan executed exactly as written.

## Next Phase Readiness

**Blockers:** None

**Recommendations for Phase 5:**
1. Priority 1: Packet Object pool (Medium-High complexity, High impact)
2. Priority 2: Inline small Bytes members in Packet::Object (Medium complexity)
3. Priority 3: make_shared for Bytes (Low complexity)
4. Priority 4: Bytes arena allocator (High complexity)

**Continue to:** 03-02-PLAN.md (Link/Identity audit)
