# Phase 8: P3 Optimization & Hardening - Context

**Gathered:** 2026-01-24
**Status:** Ready for planning

<domain>
## Phase Boundary

Complete P3 optimizations and hardening for long-term stability. Implement memory pool allocators for hot paths, graceful BLE shutdown, debug timeout variants for portMAX_DELAY sites, and document all undocumented delays/volatile usage.

Requirements: MEM-H1, MEM-H2, MEM-H3, MEM-H4, MEM-L1, CONC-H4, CONC-M4, CONC-L1, CONC-L2, CONC-L4

</domain>

<decisions>
## Implementation Decisions

### Pool/Arena Allocator Scope
- Scope limited to Bytes and Packet types only — the two hot paths identified in v1 audit
- Use fixed-size pools (not dynamic arenas) — predictable memory, simpler debugging
- On pool exhaustion: fall back to heap allocation (graceful degradation, never fail)
- Pool sizes should be build-time configurable (platformio.ini or config header)

### Shutdown Behavior
- BLE shutdown waits 10 seconds for active operations to complete
- If operations don't complete within timeout: force close + set unclean reset flag for next boot verification
- Wait scope: Claude decides which operations actually risk corruption (writes vs reads/scans)
- Graceful shutdown applies to all restart scenarios (user-initiated, OTA, watchdog recovery)

### Debug Timeout Policy
- Add debug timeout variants to ALL portMAX_DELAY sites — comprehensive coverage
- Use 5 second timeout (matches Phase 7's LVGL mutex timeout for consistency)
- On timeout: log warning and continue waiting (don't break functionality)
- Debug builds only — no overhead in production release builds

### Documentation Depth
- Document ALL undocumented delay() and volatile sites with rationale
- Comment depth: Claude decides based on how unclear each site is (brief for obvious, detailed for complex)
- Include summary table in CONTRIBUTING.md listing all timing/volatile sites
- Include reference links (datasheets, ESP32 docs, specs) where applicable

### Claude's Discretion
- Specific pool sizes for Bytes and Packet types
- Which BLE operations are "critical" enough to wait for during shutdown
- Comment depth per-site based on clarity

</decisions>

<specifics>
## Specific Ideas

- Pool sizes should be defined in platformio.ini or a central config header for easy tuning per device variant
- Summary table in CONTRIBUTING.md should list file, line, delay/volatile, and rationale for each site
- Unclean reset flag should be checked on boot to allow integrity verification

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 08-p3-optimization-hardening*
*Context gathered: 2026-01-24*
