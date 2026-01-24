---
phase: 07-p2-production-readiness
plan: 02
subsystem: memory
tags: [allocation, make_shared, deferred, COW, Bytes, PacketReceipt]
dependency-graph:
  requires: []
  provides: [single-allocation-bytes, deferred-packetreceipt]
  affects: [08-documentation]
tech-stack:
  added: []
  patterns: [make_shared-single-allocation, lazy-initialization]
key-files:
  created: []
  modified:
    - src/Bytes.cpp
    - src/Packet.h
decisions:
  - id: MEM-M1
    choice: "Use std::make_shared<Data>() for Bytes allocation"
    rationale: "Single allocation combines control block with Data object, reducing heap fragmentation"
  - id: MEM-M2
    choice: "Defer PacketReceipt allocation until first use"
    rationale: "Reduces unnecessary allocations when receipts are not accessed"
metrics:
  duration: ~3 minutes
  completed: 2026-01-24
---

# Phase 7 Plan 02: Memory Allocation Optimization Summary

**One-liner:** Single-allocation Bytes via make_shared + deferred PacketReceipt allocation for reduced heap fragmentation

## What Was Done

### Task 1: Convert Bytes::newData to use make_shared
- Replaced `new Data()` + `SharedData(data)` two-allocation pattern with `std::make_shared<Data>()` single allocation
- Updated `newData()` method to use make_shared directly
- Updated `exclusiveData()` COW copy path to use make_shared
- Removed FIXME comment about make_shared optimization (now implemented)

### Task 2: Implement deferred allocation for PacketReceipt
- Changed default constructor from `_object(new Object())` to `_object(nullptr)`
- Added `ensure_object()` helper method for lazy initialization via make_shared
- Existing `operator bool()` already returns false for null `_object`
- Existing accessor methods use `assert(_object)` to catch improper usage

## Commits

| Commit | Description |
|--------|-------------|
| fec729f | feat(07-02): convert Bytes to use make_shared for single allocation |
| 67aeb6d | feat(07-02): implement deferred allocation for PacketReceipt |

## Requirements Satisfied

- **MEM-M1:** Bytes uses make_shared for single-allocation pattern
- **MEM-M2:** PacketReceipt default constructor defers allocation

## Deviations from Plan

None - plan executed exactly as written.

## Notes

- Build environment has pre-existing errors in Link.cpp and LXMessage.cpp related to msgpack type conversion (`arduino::msgpack::bin_t<unsigned char>` vs `RNS::Bytes`). These are unrelated to this plan's changes.
- The `ensure_object()` helper is provided but accessor methods intentionally do NOT auto-allocate on read - the existing `assert(_object)` pattern correctly catches improper usage where callers forget to check `if (receipt)` before accessing.

## Key Files Modified

- **src/Bytes.cpp:** Lines 8-26 (newData) and 38-65 (exclusiveData COW path)
- **src/Packet.h:** Lines 50-52 (constructor) and 116-121 (ensure_object helper)

## Next Phase Readiness

Ready to proceed. Memory allocation optimizations complete for Bytes and PacketReceipt classes.
