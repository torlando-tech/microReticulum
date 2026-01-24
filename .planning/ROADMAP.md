# Roadmap: microReticulum

## Milestones

- ✅ v1.0 Stability Audit - Phases 1-5 (shipped 2026-01-24)
- ✅ v1.1 Stability Quick Wins - Phase 6 (shipped 2026-01-24)
- ✅ v1.2 Stability Complete - Phases 7-8 (shipped 2026-01-24)

## Phases

<details>
<summary>v1.0 Stability Audit (Phases 1-5) - SHIPPED 2026-01-24</summary>

See: .planning/milestones/v1.0-ROADMAP.md (if archived) or phase directories

- [x] Phase 1: Memory Monitoring (3/3 plans)
- [x] Phase 2: Boot Profiling (3/3 plans)
- [x] Phase 3: Memory Allocation Audit (4/4 plans)
- [x] Phase 4: Concurrency Audit (3/3 plans)
- [x] Phase 5: Synthesis (2/2 plans)

</details>

<details>
<summary>v1.1 Stability Quick Wins (Phase 6) - SHIPPED 2026-01-24</summary>

See: .planning/milestones/v1.1-ROADMAP.md

- [x] Phase 6: P1 Stability Fixes (2/2 plans)

</details>

### v1.2 Stability Complete (In Progress)

**Milestone Goal:** Complete all remaining P2 and P3 stability issues from v1 audit, making firmware production-ready.

- [x] **Phase 7: P2 Production Readiness** - Fix all P2 issues (LVGL threading, allocation patterns, BLE cache, documentation)
- [x] **Phase 8: P3 Optimization & Hardening** - Complete P3 issues (memory pools, shutdown safety, documentation)

## Phase Details

### Phase 7: P2 Production Readiness

**Goal**: Fix all P2 medium-severity issues to make firmware production-ready
**Depends on**: Phase 6 (v1.1 complete)
**Requirements**: MEM-M1, MEM-M2, MEM-M3, CONC-M1, CONC-M2, CONC-M3, CONC-M5, CONC-M6, CONC-M7, CONC-M8, CONC-M9
**Success Criteria** (what must be TRUE):
  1. All LVGL screen constructors/destructors use LVGL_LOCK (no race conditions during screen transitions)
  2. LVGL mutex uses debug timeout (5s) to detect deadlocks in debug builds
  3. BLE discovered devices cache is bounded with LRU eviction (connected devices never evicted)
  4. Bytes and PacketReceipt allocation patterns use single-allocation or deferred patterns
  5. Mutex ordering is documented in CONTRIBUTING.md
**Plans**: TBD (estimated 2-3 plans)

Plans:
- [x] 07-01: LVGL Mutex Thread Safety
- [x] 07-02: Memory Allocation Optimization
- [x] 07-03: BLE Cache Bounds
- [x] 07-04: I2S Timeout Safety
- [x] 07-05: Concurrency Documentation

### Phase 8: P3 Optimization & Hardening

**Goal**: Complete P3 optimizations and hardening for long-term stability
**Depends on**: Phase 7
**Requirements**: MEM-H1, MEM-H2, MEM-H3, MEM-L1, CONC-H4, CONC-M4, CONC-L1, CONC-L2, CONC-L4
<!-- Note: MEM-H4 (PacketReceipt lazy allocation) was completed in Phase 7 as MEM-M2 -->
**Success Criteria** (what must be TRUE):
  1. Bytes and Packet use pool/arena allocators reducing per-packet heap fragmentation
  2. Packet fixed-size members use inline buffers (saving ~150 bytes/packet)
  3. BLE shutdown waits for active operations to complete (no use-after-free on restart)
  4. All portMAX_DELAY sites have debug timeout variants to detect stuck tasks
  5. Undocumented delays and volatile usage have rationale comments
**Plans**: 8 plans in 5 waves

Plans:
- [x] 08-01-PLAN.md - Quick wins: toHex reserve, ArduinoJson v7 API migration
- [x] 08-02-PLAN.md - Debug timeouts for remaining portMAX_DELAY sites
- [x] 08-03-PLAN.md - Volatile and delay rationale documentation
- [x] 08-04-PLAN.md - Pool allocators and Packet inline buffers
- [x] 08-05-PLAN.md - BLE graceful shutdown with timeout
- [x] 08-06-PLAN.md - Gap closure: BytesPool integration (MEM-H1)
- [x] 08-07-PLAN.md - Gap closure: Packet/Receipt pool integration (MEM-H2)
- [x] 08-08-PLAN.md - Pool exhaustion visibility and graceful failure handling

## Progress

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 1. Memory Monitoring | v1.0 | 3/3 | Complete | 2026-01-23 |
| 2. Boot Profiling | v1.0 | 3/3 | Complete | 2026-01-23 |
| 3. Memory Allocation Audit | v1.0 | 4/4 | Complete | 2026-01-24 |
| 4. Concurrency Audit | v1.0 | 3/3 | Complete | 2026-01-24 |
| 5. Synthesis | v1.0 | 2/2 | Complete | 2026-01-24 |
| 6. P1 Stability Fixes | v1.1 | 2/2 | Complete | 2026-01-24 |
| 7. P2 Production Readiness | v1.2 | 5/5 | Complete | 2026-01-24 |
| 8. P3 Optimization & Hardening | v1.2 | 8/8 | Complete | 2026-01-24 |

**Total:** 8 phases, 30 plans completed (v1.0-v1.2)

---
*Last updated: 2026-01-24 after Phase 8 complete with gap closures*
