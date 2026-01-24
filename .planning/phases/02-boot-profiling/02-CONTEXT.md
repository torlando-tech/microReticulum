# Phase 2: Boot Profiling - Context

**Gathered:** 2026-01-23
**Status:** Ready for planning

<domain>
## Phase Boundary

Profile boot sequence timing and apply configuration optimizations to reduce boot time to under 5 seconds. Includes timing instrumentation, configuration changes, and validation. Does not include architectural changes to boot order or refactoring init code.

</domain>

<decisions>
## Implementation Decisions

### Profiling Granularity
- Per-function timing: every init function timed individually (fine-grained)
- Show both per-item duration AND cumulative total at each checkpoint
- Distinguish 'init time' from 'wait/block time' for each subsystem (blocking waits like WiFi connect timed separately)
- BOOT_PROFILING_ENABLED build flag (same pattern as memory instrumentation)

### Output Format
- Serial output during boot (real-time visibility)
- SD card file for persistence (plain text log format)
- Rotate last 5 boot profiles (keep history, don't fill storage)

### Optimization Approach
- Apply all identified optimizations (not document-only)
- Moderate risk tolerance: accept some risk for meaningful gains, test each change individually
- Individual flags for each optimization (toggleable for testing/debugging)
- Document changes in separate BOOT_OPTIMIZATIONS.md file (not inline comments)

### Validation Method
- Before/after comparison against baseline profile
- 'Boot complete' = UI responsive (LVGL interactive), not just setup() return or network ready
- Phase blocks until 5-second target achieved (validation failure = phase incomplete)

### Claude's Discretion
- Multiple boots vs single boot for validation (based on observed variance)
- Exact log format and timing resolution
- Order of optimization application

</decisions>

<specifics>
## Specific Ideas

- Follow same build flag pattern established in Phase 1 (MEMORY_INSTRUMENTATION_ENABLED → BOOT_PROFILING_ENABLED)
- SD card file rotation similar to embedded logging best practices

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 02-boot-profiling*
*Context gathered: 2026-01-23*
