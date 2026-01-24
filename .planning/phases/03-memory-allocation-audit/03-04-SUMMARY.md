---
phase: 03-memory-allocation-audit
plan: 04
subsystem: memory-audit
tags: [memory-pools, audit-consolidation, phase-completion, documentation]
dependency-graph:
  requires: [03-01, 03-02, 03-03]
  provides: [MEM-07, consolidated-audit, phase-5-backlog]
  affects: [05-memory-optimization]
tech-stack:
  added: []
  patterns: [fixed-pool-inventory, severity-classification, phase-backlog]
key-files:
  created:
    - .planning/phases/03-memory-allocation-audit/03-AUDIT.md
    - docs/MEMORY_AUDIT.md
  modified: []
decisions:
  - id: pool-inventory
    choice: "Documented 40+ memory pools across codebase"
    reason: "Complete MEM-07 requirement"
  - id: severity-classification
    choice: "0 Critical, 5 High, 4 Medium, 4 Low issues"
    reason: "Prioritize Phase 5 work"
metrics:
  duration: 5min
  completed: 2026-01-24
---

# Phase 3 Plan 04: Memory Pools Documentation and Audit Consolidation Summary

**One-liner:** Documented 40+ memory pools (~550KB static, ~330KB PSRAM), consolidated all findings into definitive audit report with prioritized Phase 5 backlog (0 Critical, 5 High, 4 Medium, 4 Low issues).

## What Was Done

### Task 1: Complete memory pools documentation (MEM-07)

Documented all memory pools across the codebase:

**Transport Pools (20+):** ~21KB static allocation
- announce_table (8), destination_table (16), reverse_table (8), link_table (8)
- held_announces (8), tunnels (16), announce_rate_table (8), path_requests (8)
- receipts (8), packet_hashlist (64), discovery_pr_tags (32)
- pending_links (4), active_links (4), control_hashes (8), control_destinations (8)
- announce_handlers (8), local_client_interfaces (8), interfaces (8), destinations (32)
- discovery_path_requests (32), pending_local_path_requests (32)

**Identity Pools:** ~30KB
- known_destinations (192 slots, PSRAM, ~23KB)
- known_ratchets (128 slots, static, ~7.3KB)

**Link/Channel Pools:** ~5KB per link
- incoming_resources (8), outgoing_resources (8), pending_requests (8)
- rx_ring_pool (16), tx_ring_pool (16)

**LXMF Pools:**
- LXMRouter: pending_outbound (16), pending_inbound (16), failed_outbound (8)
- direct_links (8), pending_proofs (16), transient_ids (64), pending_prop_resources (16)
- MessageStore: conversations (32), message_hashes/conv (256)
- PropagationNodeManager: nodes (32)
- LXMessage: fields (16)

**BLE Pools:**
- BLEReassembler: pending_reassemblies (8), fragments_per_reassembly (32)
- BLEPeerManager: peers_by_identity (8), peers_by_mac_only (8), mac_to_identity (8)
- BLEIdentityManager: address_identity (16), handshakes (4)

**Other Pools:**
- Interface: announce_queue (32)
- SegmentAccumulator: pending_transfers (8), segments_per_transfer (64)
- Destination: request_handlers (8), path_responses (8), ratchets (128)

### Task 2: Create consolidated audit report (03-AUDIT.md)

Created comprehensive audit report consolidating findings from plans 03-01, 03-02, 03-03:

**Requirements Covered:**
- MEM-03: shared_ptr patterns (14 new/shared_ptr, 2 make_shared)
- MEM-04: Packet/Bytes allocations (per-packet fragmentation risk)
- MEM-05: ArduinoJson usage (1 deprecated pattern)
- MEM-06: PSRAM verification (confirmed in Bytes, Identity, LVGL)
- MEM-07: Memory pools (40+ pools documented)

**Issue Summary:**
| Severity | Count | Key Issues |
|----------|-------|------------|
| Critical | 0 | All large allocations use PSRAM |
| High | 5 | Per-packet Bytes/Object allocations |
| Medium | 4 | make_shared patterns, ArduinoJson migration |
| Low | 4 | Minor optimizations |

**Phase 5 Backlog Created:**
- P2-1: Packet Object Pool (High impact, Medium-High complexity)
- P2-2: Inline Small Bytes Members (Medium-High impact, Medium complexity)
- P2-3: Resource Vector Pre-allocation (Medium impact, Low complexity)
- P3-1: make_shared conversion (Low-Medium impact, Low complexity)
- P3-2: ArduinoJson migration (Low impact, Low complexity)

### Task 3: Copy report to docs/ for long-term reference

Created `/home/tyler/repos/public/microReticulum/docs/MEMORY_AUDIT.md` as reference copy with note indicating source of truth is in planning directory.

## Key Findings

**Memory Pool Totals:**
| Category | Size | PSRAM |
|----------|------|-------|
| Transport | ~21KB | No (static) |
| Identity | ~30KB | Partial (destinations) |
| MessageStore | ~288KB | Should consider |
| Link/Channel (per link) | ~5KB | No |
| BLE pools | ~200KB (fragments) | Should consider |
| LVGL buffers | ~307KB | Yes |

**Total Static Pool Memory:** ~550KB (excluding LVGL)
**PSRAM Usage:** ~330KB (Identity + LVGL)

**Overflow Behaviors:**
- Returns nullptr/false: Most pools
- Ring buffer overwrite: packet_hashlist, transient_ids, message queues
- Cull oldest: known_destinations, known_ratchets, propagation_nodes

## Files Created

| File | Purpose |
|------|---------|
| .planning/phases/03-memory-allocation-audit/03-AUDIT.md | Complete audit report (584 lines) |
| docs/MEMORY_AUDIT.md | Reference copy for long-term access |

## Commits

| Hash | Message |
|------|---------|
| 46719eb | docs(03-04): create consolidated memory allocation audit report |
| 5809e7d | docs(03-04): copy audit report to docs/ for long-term reference |

## Deviations from Plan

None - plan executed exactly as written.

## Next Phase Readiness

**Phase 3 Complete:**
- All MEM requirements satisfied (MEM-03 through MEM-07)
- Audit findings documented with FIXME(frag) comments
- Phase 5 backlog created with prioritized fixes

**Ready for Phase 4 (Watchdog Analysis) or Phase 5 (Memory Optimization):**
- Clear priorities established for optimization work
- No blockers identified

**Recommendations for Phase 5:**
1. Start with Resource Vector Pre-allocation (quick win, low risk)
2. Implement Packet Object Pool (highest impact)
3. ArduinoJson migration (low risk cleanup)

---

*Plan 03-04 complete*
*Phase 3 (Memory Allocation Audit) complete*
