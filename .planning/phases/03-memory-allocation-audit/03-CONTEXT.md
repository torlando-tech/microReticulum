# Phase 3: Memory Allocation Audit - Context

**Gathered:** 2026-01-24
**Status:** Ready for planning

<domain>
## Phase Boundary

Audit all significant memory allocation patterns across the codebase and document fragmentation risk assessment. This phase audits and documents — it does NOT implement fixes. Fixes are captured for Phase 5 backlog.

</domain>

<decisions>
## Implementation Decisions

### Audit Scope & Depth
- Full codebase audit — every source file systematically
- Third-party libraries (ArduinoJson, NimBLE, etc.) fully audited, same as project code
- All shared_ptr creation sites counted and categorized (every make_shared/shared_ptr constructor, grouped by pattern)
- Allocation frequency estimated for each site (startup-only, per-packet, periodic, etc.)

### Finding Classification
- 4-tier severity: Critical/High/Medium/Low
- Risk assessment uses combined size + frequency (both factors into risk score)
- Detailed fix recommendations included (code patterns and specific changes)
- Similar issues grouped by pattern with count, listing worst examples

### Documentation Format
- Report + inline source comments
- Inline comment format: `// FIXME(frag): description`
- Report lives in both `.planning/phases/03-*/03-AUDIT.md` and `docs/MEMORY_AUDIT.md`
- Phase directory is source of truth, copy to docs/ for long-term reference

### PSRAM vs Heap Criteria
- Large heap allocations without PSRAM are Critical severity
- Audit verifies PSRAM usage via both static analysis and runtime verification
- PSRAMAllocator usage audited for consistency across entire codebase

### Claude's Discretion
- Exact size threshold for PSRAM requirement (Claude determines based on ESP32 constraints)
- Summary table format and structure for readability

</decisions>

<specifics>
## Specific Ideas

No specific requirements — open to standard approaches for the audit methodology.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 03-memory-allocation-audit*
*Context gathered: 2026-01-24*
