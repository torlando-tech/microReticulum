# Roadmap: microReticulum

## Milestones

- ✅ **v1.0 Stability Audit** - Phases 1-5 (shipped 2026-01-24)
- ✅ **v1.1 Stability Quick Wins** - Phase 6 (shipped 2026-01-24)
- 📋 **v1.2 Deeper Stability** - Phases 7+ (planned)

## Phases

<details>
<summary>✅ v1.0 Stability Audit (Phases 1-5) - SHIPPED 2026-01-24</summary>

### Phase 1: Memory Monitoring
**Goal**: Establish runtime visibility into heap and stack usage
**Plans**: 3 plans

Plans:
- [x] 01-01: Core monitoring infrastructure
- [x] 01-02: SPIFFS integration for persistence
- [x] 01-03: Testing and validation

### Phase 2: Boot Profiling
**Goal**: Identify boot sequence bottlenecks with millisecond precision
**Plans**: 3 plans

Plans:
- [x] 02-01: Core profiling infrastructure
- [x] 02-02: SPIFFS persistence integration
- [x] 02-03: Testing and baseline establishment

### Phase 3: Memory Allocation Audit
**Goal**: Document all memory pools and identify fragmentation risks
**Plans**: 4 plans

Plans:
- [x] 03-01: Static allocation audit
- [x] 03-02: PSRAM allocation audit
- [x] 03-03: Per-packet allocation patterns
- [x] 03-04: Memory audit consolidation

### Phase 4: Concurrency Audit
**Goal**: Validate thread-safety across all subsystems
**Plans**: 3 plans

Plans:
- [x] 04-01: LVGL thread safety
- [x] 04-02: BLE subsystem thread safety
- [x] 04-03: Core library thread safety

### Phase 5: Synthesis
**Goal**: Prioritize findings into actionable backlog
**Plans**: 2 plans

Plans:
- [x] 05-01: WSJF prioritization and backlog creation
- [x] 05-02: Documentation and milestone completion

</details>

### ✅ v1.1 Stability Quick Wins (Complete)

**Milestone Goal:** Fix the 5 highest-priority stability issues (P1 items with WSJF ≥ 3.0) to establish firmware stability baseline.

#### Phase 6: P1 Stability Fixes
**Goal**: Eliminate critical memory and concurrency issues preventing reliable extended operation
**Depends on**: Phase 5 (backlog created)
**Requirements**: MEM-01, MEM-02, CONC-01, CONC-02, CONC-03
**Success Criteria** (what must be TRUE):
  1. Firmware compiles cleanly with no ODR violations or linker warnings
  2. Large file transfers complete without heap fragmentation from vector reallocations
  3. Task starvation and deadlocks are detected by watchdog timer
  4. UI remains responsive during stamp generation operations
  5. Concurrent BLE operations execute without race conditions or crashes
**Plans**: 2 plans

Plans:
- [x] 06-01: Fix memory issues (ODR violation in Persistence, pre-allocate ResourceData vectors)
- [x] 06-02: Fix concurrency issues (TWDT enablement, LXStamper yield, BLE mutex protection)

## Progress

**Execution Order:**
Phases execute in numeric order: 1 → 2 → 3 → 4 → 5 → 6

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 1. Memory Monitoring | v1.0 | 3/3 | Complete | 2026-01-23 |
| 2. Boot Profiling | v1.0 | 3/3 | Complete | 2026-01-23 |
| 3. Memory Allocation Audit | v1.0 | 4/4 | Complete | 2026-01-24 |
| 4. Concurrency Audit | v1.0 | 3/3 | Complete | 2026-01-24 |
| 5. Synthesis | v1.0 | 2/2 | Complete | 2026-01-24 |
| 6. P1 Stability Fixes | v1.1 | 2/2 | Complete | 2026-01-24 |

---
*Last updated: 2026-01-24 after Phase 6 execution complete*
