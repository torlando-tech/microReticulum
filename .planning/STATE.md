# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-01-23)

**Core value:** Identify and prioritize root causes of instability for reliable extended operation
**Current focus:** Phase 3 - Memory Allocation Audit

## Current Position

Phase: 3 of 5 IN PROGRESS (Memory Allocation Audit)
Plan: 03 of 4 COMPLETE
Status: Executing Phase 3 plans
Last activity: 2026-01-24 — Completed 03-01, 03-02, 03-03 (core data path, shared_ptr, persistence audits)

Progress: [#######---] 70%

## Performance Metrics

**Velocity:**
- Total plans completed: 7
- Average duration: 4 min
- Total execution time: 30 min

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 01-memory-instrumentation | 2 | 4min | 2min |
| 02-boot-profiling | 4 | 8min | 2min |
| 03-memory-allocation-audit | 1 | 18min | 18min |

**Recent Trend:**
- Last 5 plans: 02-02 (2min), 02-03 (2min), 02-04 (3min), 03-01 (unknown), 03-02 (18min)
- Trend: Audit plans slower (more reading/analysis)

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

### Memory Allocation Audit Findings

**03-01: Core Data Path (Bytes, Packet, Transport)**
- PSRAM verified: Bytes uses PSRAMAllocator for all vector storage
- Transport well-optimized: 15+ fixed-size pools (~21KB static)
- Packet is main concern: 10+ allocations per packet (Object + 9 Bytes)
- 8 issues total: 0 Critical, 5 High, 2 Medium, 1 Low
- Key recommendation: Packet Object pool for Phase 5

**03-02: shared_ptr and Session Objects**
- 14 sites use `new T()` + `shared_ptr` pattern (2 allocations)
- 2 sites use `make_shared` (1 allocation) - Buffer.cpp, BLEPlatform.cpp
- Identity pools use PSRAM via heap_caps_aligned_alloc (verified)
- Fixed pools in Link/Channel eliminate fragmentation
- Resource vectors resize during transfers - fragmentation risk

**Fixed Pool Inventory:**
| Pool | Size | Location |
|------|------|----------|
| known_destinations | 192 | Identity (PSRAM) |
| known_ratchets | 128 | Identity (static) |
| incoming_resources | 8 | LinkData |
| outgoing_resources | 8 | LinkData |
| pending_requests | 8 | LinkData |
| rx_ring_pool | 16 | ChannelData |
| tx_ring_pool | 16 | ChannelData |

### Pending Todos

None yet.

### Blockers/Concerns

**Resolved:**
- `PSRAMAllocator.h` - Created in src/
- `partitions.csv` - Created in examples/lxmf_tdeck/

**Identified for Phase 5:**
- Resource vectors (`_parts`, `_hashmap`) resize during transfers
- 14 sites could use `make_shared` for ~40 bytes savings each

## Session Continuity

Last session: 2026-01-24
Completed: 03-02-PLAN.md (shared_ptr and session object audit)
Next: 03-03-PLAN.md (Interface and Packet allocation patterns)

---
*Phase 3 Plan 2 complete. Continuing Phase 3.*
