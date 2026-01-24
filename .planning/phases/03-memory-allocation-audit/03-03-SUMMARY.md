---
phase: 03
plan: 03
subsystem: persistence
tags: [arduinojson, json, persistence, lvgl, ble, memory-audit]
dependency-graph:
  requires: []
  provides: [MEM-05]
  affects: [05-memory-optimization]
tech-stack:
  added: []
  patterns: [document-reuse, pool-based-allocation]
key-files:
  created:
    - .planning/phases/03-memory-allocation-audit/03-03-FINDINGS.md
  modified: []
decisions:
  - id: dec-03-03-01
    description: "DynamicJsonDocument in Persistence requires migration to JsonDocument"
    rationale: "Deprecated ArduinoJson 6 API; ArduinoJson 7.4.2 is configured"
metrics:
  duration: 3min
  completed: 2026-01-24
---

# Phase 3 Plan 03: ArduinoJson and Persistence Audit Summary

ArduinoJson 7.4.2 is properly configured; one deprecated v6 pattern found in Persistence layer requiring migration. UI and BLE subsystems confirmed using correct allocation patterns.

## What Was Done

1. **ArduinoJson Usage Audit (MEM-05)**
   - Searched all `JsonDocument`, `DynamicJsonDocument`, `StaticJsonDocument` usage
   - Found 2 deprecated patterns in Persistence.h/cpp using DynamicJsonDocument
   - Confirmed MessageStore uses correct JsonDocument member with clear() reuse
   - Documented all deserializeJson/serializeJson call sites (15 total)

2. **UI Subsystem Verification**
   - All 8 screen objects allocated at startup in UIManager::init()
   - No per-frame or periodic allocations found
   - LVGL buffers correctly allocated in PSRAM (307KB double-buffered)

3. **BLE Subsystem Verification**
   - BLEReassembler: 8 pending slots with 32 fragments each
   - BLEPeerManager: 8-slot pools for peers, MACs, handles
   - BLEIdentityManager: 16-slot address-identity pool
   - One minor allocation: BLEFragmenter::fragment() returns std::vector

## Key Findings

| ID | Severity | Description |
|----|----------|-------------|
| AJ-01 | Medium | DynamicJsonDocument in Persistence.h:475 - deprecated |
| AJ-02 | Medium | Duplicate static definition in .h and .cpp |
| AJ-03 | Low | Fixed 8KB document size may be suboptimal |
| BLE-01 | Low | Temporary vector in BLEFragmenter::fragment() |

## Decisions Made

1. **ArduinoJson Migration Required**: Persistence.h/cpp must migrate from `DynamicJsonDocument` to `JsonDocument` for ArduinoJson 7 compatibility

## Files Produced

- `.planning/phases/03-memory-allocation-audit/03-03-FINDINGS.md` - Complete audit with migration checklist

## Deviations from Plan

None - plan executed exactly as written.

## Next Phase Readiness

Phase 5 (Memory Optimization) migration tasks documented:
1. Migrate Persistence to ArduinoJson 7 API (Medium priority)
2. Consolidate duplicate static variable definition (Low priority)
3. Consider BLEFragmenter output parameter optimization (Low priority, optional)

---

*Plan 03-03 complete*
