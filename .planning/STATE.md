# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-01-23)

**Core value:** Identify and prioritize root causes of instability for reliable extended operation
**Current focus:** MILESTONE 1 COMPLETE - Stability Audit delivered

## Current Position

Phase: 5 of 5 COMPLETE (Synthesis)
Plan: 01 of 1 COMPLETE
Status: Synthesis complete, BACKLOG.md delivered
Last activity: 2026-01-24 - Completed 05-01 (Create Prioritized Backlog)

Progress: [##############] 100% (Milestone 1)

## Performance Metrics

**Velocity:**
- Total plans completed: 14
- Average duration: 5 min
- Total execution time: ~65 min

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 01-memory-instrumentation | 2 | 4min | 2min |
| 02-boot-profiling | 4 | 8min | 2min |
| 03-memory-allocation-audit | 4 | 26min | 6.5min |
| 04-concurrency-audit | 4 | 24min | 6min |
| 05-synthesis | 1 | 3min | 3min |

**Recent Trend:**
- Last 5 plans: 04-02 (4min), 04-03 (4min), 04-04 (4min), 05-01 (3min)
- Milestone 1 complete in 65 min across 14 plans

*Updated after each plan completion*

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [Init]: Data-driven approach - instrument first, then audit based on findings
- [Init]: Boot and memory audits run in parallel after instrumentation
- [01-01]: RNS::Instrumentation namespace for new instrumentation code
- [01-01]: Static 256-byte buffers for log formatting (avoid stack pressure)
- [01-01]: Warn at 50% fragmentation threshold, 256 bytes stack remaining
- [01-02]: Initialize memory monitor after LVGL task starts (before Reticulum)
- [01-02]: 30-second monitoring interval per CONTEXT.md
- [02-01]: Use millis() for boot timing (sufficient precision, already in codebase)
- [02-01]: First markStart() establishes boot start time (no explicit init)
- [02-01]: Wait time tracked separately from init time
- [02-02]: Wrap setup_*() calls in main setup() rather than inside each function
- [02-02]: WAIT markers inside setup_wifi() and setup_lxmf() where waits occur
- [02-03]: Disable PSRAM memory test for ~2s boot time savings
- [02-04]: Enable BOOT_REDUCED_LOGGING with CORE_DEBUG_LEVEL=2
- [02-04]: 5s target not achievable via config - reticulum init is 2.5s
- [03-02]: new/shared_ptr pattern acceptable for startup allocations
- [03-02]: Resource vectors identified as fragmentation risk during transfers
- [03-03]: DynamicJsonDocument in Persistence requires migration to JsonDocument
- [03-04]: 40+ memory pools documented, all with overflow protection
- [04-01]: LVGL event callbacks run in LVGL task context (no explicit lock needed)
- [04-01]: Screen constructors/destructors should have LVGL_LOCK() for defensive coding
- [04-01]: Recursive mutex correctly handles nested LVGL calls
- [04-04]: 4-level mutex hierarchy: LVGL (outermost) -> BLE Interface -> NimBLE Platform -> Spinlocks (innermost)
- [04-04]: Implicit mutex ordering is safe - no cross-subsystem nesting detected
- [05-01]: WSJF scoring (Severity/Effort) for prioritization
- [05-01]: 30 issues consolidated into single BACKLOG.md

### Boot Profiling Findings

**Final boot timing (with all optimizations):**
- Total: 9,704ms
- Init: 5,336ms (336ms over 5s target)
- Wait: 4,368ms (45% of boot)

**Longest phases (init time):**
1. reticulum: 2,580ms (cryptographic operations)
2. ui_manager: 957ms
3. hardware: 509ms
4. lvgl: 482ms
5. gps: 553ms (excluding 368ms GPS sync wait)

**Blocking operations identified:**
- TCP stabilization: 3,000ms fixed delay
- WiFi connect: ~1,000ms (30s timeout)
- GPS sync: ~370-950ms variable

### Memory Allocation Audit Findings (COMPLETE)

**Final Summary:**
- **Total Issues:** 13 (0 Critical, 5 High, 4 Medium, 4 Low)
- **Memory Pools:** 40+ documented
- **Total Pool Memory:** ~550KB (excluding LVGL)
- **PSRAM Usage:** ~330KB (Identity + LVGL)

**Complete Pool Inventory:**
| Category | Pools | Size | PSRAM |
|----------|-------|------|-------|
| Transport | 20+ | ~21KB | No |
| Identity | 2 | ~30KB | Partial |
| MessageStore | 2 | ~288KB | Should consider |
| Link/Channel | 5 | ~5KB/link | No |
| BLE | 8 | ~200KB | Should consider |
| LXMF | 10+ | Variable | No |

### Concurrency Audit Findings (COMPLETE)

**Phase 4 Summary:**
- **Total Issues:** 17 (0 Critical, 4 High, 9 Medium, 4 Low)
- **Mutexes Documented:** 5 primitives across 3 subsystems
- **Deadlock Risk:** LOW (no cross-subsystem mutex nesting)

### Phase 5 Synthesis Findings (COMPLETE)

**Backlog Created:** `.planning/phases/05-synthesis/BACKLOG.md`

**Issue Distribution:**
- **Total Issues:** 30 (13 memory + 17 concurrency)
- **By Severity:** 0 Critical, 9 High, 13 Medium, 8 Low
- **By Priority:** P1 (5), P2 (11), P3 (13), P4 (1)

**Top P1 Issues (Fix Immediately):**
1. CONC-H1: TWDT not configured (WSJF 3.50)
2. CONC-H2: LXStamper CPU hogging (WSJF 3.50)
3. CONC-H3: Pending queues not thread-safe (WSJF 3.50)
4. MEM-H5: Resource vectors resize during transfers (WSJF 3.50)
5. MEM-M4: Duplicate static definition (WSJF 4.00)

**Sprint Recommendations:**
- Sprint 1: P1 issues (2-3 days)
- Sprint 1-2: P2 issues (4-5 days)
- Sprint 2-3: P3 issues opportunistically

### Pending Todos

None - Milestone 1 complete.

### Blockers/Concerns

**Resolved:**
- `PSRAMAllocator.h` - Created in src/
- `partitions.csv` - Created in examples/lxmf_tdeck/
- All audit reports complete
- Prioritized backlog delivered

**Deferred to Implementation:**
- 30 stability issues documented in BACKLOG.md
- Top priority: TWDT configuration, thread-safe BLE queues

## Session Continuity

Last session: 2026-01-24
Completed: 05-01-PLAN.md (Create Prioritized Backlog)
Milestone: 1 (Stability Audit) COMPLETE
Next: Implementation sprints from BACKLOG.md

---
*MILESTONE 1 COMPLETE*
*Deliverable: .planning/phases/05-synthesis/BACKLOG.md*
*30 issues prioritized by WSJF, ready for implementation*
