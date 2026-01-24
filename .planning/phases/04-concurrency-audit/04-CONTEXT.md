# Phase 4: Concurrency Audit - Context

**Gathered:** 2026-01-24
**Status:** Ready for planning

<domain>
## Phase Boundary

Audit all threading patterns in the firmware and document risk assessment for deadlock, race conditions, and resource leaks. Output is documentation with findings and fix recommendations — no code changes in this phase.

</domain>

<decisions>
## Implementation Decisions

### LVGL Threading Scope
- Full widget audit: all LVGL API calls, tick/handler, and custom widgets
- Assess pattern quality: evaluate whether current mutex pattern is robust (RAII vs manual)
- Claude's discretion on LVGL calls from ISR/timer contexts — assess based on actual callback context
- Note allocation concerns encountered while auditing threading (don't ignore, add to backlog)

### NimBLE Lifecycle Focus
- Full lifecycle audit: init/deinit cycles AND connection state machine
- Audit all callbacks: GAP, GATT, and connection callbacks for thread safety and blocking
- Document NimBLE task stack sizes with high water mark risk assessment
- Include advertising/scanning threading patterns for race condition analysis

### Finding Handling
- Document and continue: log all findings, address in synthesis
- Include inline fix recommendations with each finding as documented
- Severity is impact-based:
  - Critical = crash or data corruption
  - High = deadlock risk
  - Medium = race condition potential
  - Low = code smell / best practice violation
- Include watchdog analysis: feeding, yields, blocking operations that could trigger WDT

### Output Structure
- Per-subsystem documentation: 04-LVGL.md, 04-NIMBLE.md, 04-TASKS.md, etc.
- Include brief code snippets (3-5 lines) showing problematic patterns
- Create 04-SUMMARY.md with issue counts, severity breakdown, and cross-references
- Include ASCII diagrams showing task relationships and mutex dependencies

### Claude's Discretion
- Exact subsystem document breakdown (may combine small areas)
- Diagram complexity based on actual codebase patterns
- Which callbacks warrant deep analysis vs. skim

</decisions>

<specifics>
## Specific Ideas

- Similar structure to Phase 3 memory audit (per-subsystem docs with summary)
- Watchdog is part of this audit, not separate
- Stack sizes cross-reference Phase 1 monitoring data where relevant

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 04-concurrency-audit*
*Context gathered: 2026-01-24*
