# Phase 7: P2 Production Readiness - Context

**Gathered:** 2026-01-24
**Status:** Ready for planning

<domain>
## Phase Boundary

Fix all P2 medium-severity issues to make firmware production-ready. Specifically: LVGL threading safety, allocation patterns for Bytes/PacketReceipt, BLE cache bounds, and mutex ordering documentation.

Requirements: MEM-M1, MEM-M2, MEM-M3, CONC-M1, CONC-M2, CONC-M3, CONC-M5, CONC-M6, CONC-M7, CONC-M8, CONC-M9

</domain>

<decisions>
## Implementation Decisions

### LVGL Locking Approach
- Claude decides where locks are needed (based on audit findings, not blanket coverage)
- Debug builds: 5-second timeout on LVGL mutex, assert/crash on timeout
- Release builds: No timeout (portMAX_DELAY) — production waits forever

### BLE Cache Eviction
- Cache size: 16 devices maximum
- Eviction policy: Insertion-order based (simpler than LRU, no timestamps)
- Connected devices: Never evicted — protection is absolute
- When full: Evict oldest discovered-but-not-connected device

### Allocation Pattern Changes
- Claude decides single-allocation vs deferred based on usage analysis
- API breakage acceptable if it enables cleaner patterns
- Rollout: Incremental (one class at a time)
- Priority order: Bytes first, then PacketReceipt

### Mutex Documentation
- Location: Dedicated CONCURRENCY.md file (not CONTRIBUTING.md)
- Visual: Mermaid diagram for lock ordering
- Detail level: Full documentation per mutex (purpose, ordering, typical holders, timeout behavior)
- Scope: All concurrency patterns (mutexes, task priorities, queues, event groups)

### Claude's Discretion
- Which specific LVGL operations need locking (based on audit)
- Bytes allocation strategy (single-allocation vs deferred)
- Exact structure and sections of CONCURRENCY.md

</decisions>

<specifics>
## Specific Ideas

- Debug timeout should "force investigation during development" — hard failures preferred over silent issues
- CONCURRENCY.md should be comprehensive — "go big" on documentation scope
- Incremental rollout for allocation changes — easier to isolate regressions

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 07-p2-production-readiness*
*Context gathered: 2026-01-24*
