# Phase 1: Memory Instrumentation - Context

**Gathered:** 2026-01-23
**Status:** Ready for planning

<domain>
## Phase Boundary

Establish runtime memory monitoring that captures heap, stack, and fragmentation metrics. This phase delivers instrumentation code that logs baseline data for diagnostic purposes. Memory optimization and fixes based on findings belong in later phases.

</domain>

<decisions>
## Implementation Decisions

### Output format & verbosity
- Human-readable format (e.g., "Free heap: 123456 bytes, Largest block: 98765")
- Moderate verbosity by default: heap essentials plus task stack high water marks
- Fragmentation percentage shown as raw number without severity labels

### Triggering & frequency
- Periodic timer-based logging (not on-demand)
- Default interval: 30 seconds
- Interval is configurable via build flag or runtime setting
- Auto-start on boot when instrumentation is enabled

### Output destination
- Dual output: serial console AND SD card log file
- Session-based log files: `memory_YYYYMMDD_HHMMSS.txt`
- Include header line describing fields at start of each log file
- If SD card unavailable: warn once at boot, then continue with serial only

### Overhead tolerance
- Minimal heap overhead (<1KB) — use static buffers
- Instrumentation completely removable via build flag (#ifdef, zero overhead when disabled)
- Don't over-optimize CPU — 30s intervals are negligible

### Claude's Discretion
- Runtime verbosity toggle mechanism (if implemented)
- Dedicated task vs timer callback for logging execution
- Exact log format string and field ordering

</decisions>

<specifics>
## Specific Ideas

No specific requirements — open to standard approaches for ESP32/FreeRTOS instrumentation patterns.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 01-memory-instrumentation*
*Context gathered: 2026-01-23*
