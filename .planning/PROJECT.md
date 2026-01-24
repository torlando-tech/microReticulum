# microReticulum Stability Audit

## What This Is

A comprehensive audit of the microReticulum codebase — a C++ implementation of the Reticulum network stack and LXMF messaging protocol for the Lilygo T-Deck Plus and other ESP32-based devices. The v1 stability audit is complete, delivering instrumentation modules and a prioritized backlog of 30 issues.

## Core Value

Identify and prioritize the root causes of instability so the firmware can run reliably for extended periods without crashes or performance degradation.

## Current Milestone: v1.1 Stability Quick Wins

**Goal:** Fix the 5 highest-priority stability issues identified in v1 audit (P1 items with WSJF ≥ 3.0).

**Target fixes:**
- MEM-M4: Duplicate static definition in Persistence (WSJF 4.00)
- CONC-H1: Enable Task Watchdog Timer for application tasks (WSJF 3.50)
- CONC-H2: Fix LXStamper CPU hogging during stamp generation (WSJF 3.50)
- CONC-H3: Add mutex protection to BLE pending queues (WSJF 3.50)
- MEM-H5: Pre-allocate resource vectors to prevent resize during transfers (WSJF 3.50)

## v1 Audit Summary

**Delivered 2026-01-24:**
- MemoryMonitor module: 30s periodic heap/stack monitoring
- BootProfiler module: millisecond timing with SPIFFS persistence
- 40+ memory pools documented (~550KB static, ~330KB PSRAM)
- 17 concurrency issues identified across LVGL, NimBLE, FreeRTOS
- BACKLOG.md with 30 WSJF-prioritized issues

**Boot timing baseline:**
- Total: 9,704ms (init: 5,336ms, wait: 4,368ms)
- Largest phase: reticulum init at 2.5s (cryptographic operations)

**Key findings:**
- No critical blocking issues
- 9 High severity, 13 Medium, 8 Low issues documented
- Threading architecture is sound (implicit mutex ordering)
- Per-packet allocations are main fragmentation concern

## Requirements

### Validated

- Reticulum protocol implementation — existing
- LXMF message exchange with Python clients — existing
- BLE mesh interface with multi-platform support — existing
- LVGL-based UI framework — existing
- FreeRTOS task-based concurrency — existing
- Identity management and cryptography — existing
- Message persistence and routing — existing
- Heap/stack monitoring — v1
- Boot sequence profiling — v1
- Memory allocation audit — v1
- Concurrency pattern audit — v1
- Prioritized stability backlog — v1

### Active

- [ ] MEM-M4: Fix duplicate static definition in Persistence
- [ ] CONC-H1: Enable TWDT for application tasks
- [ ] CONC-H2: Fix LXStamper yield frequency (100 → 10 rounds)
- [ ] CONC-H3: Add mutex to BLE pending queues
- [ ] MEM-H5: Pre-allocate ResourceData vectors

### Out of Scope

- New feature development — stability first
- Python reference implementation — focus is C++ code
- Hardware driver bugs — unless directly causing stability issues
- Test coverage improvements — unless directly related to identified issues

## Context

**Codebase Structure:**
- Core networking: Transport, Packet, Interface layers
- Cryptography: Ed25519, X25519, AES, Fernet, Ratchet
- Identity & Destination management with fixed-size pools
- Link & Channel for encrypted bidirectional communication
- BLE subsystem with NimBLE platform implementation
- LXMF message routing and persistence
- LVGL UI with FreeRTOS task scheduling
- T-Deck hardware abstraction
- Instrumentation: MemoryMonitor, BootProfiler (new in v1)

**Technical Environment:**
- ESP32-S3 (T-Deck Plus) with 8MB flash, PSRAM
- C++11 with PlatformIO/Arduino framework
- FreeRTOS for task scheduling
- ArduinoJson 7.4.2+, MsgPack 0.4.2+
- Custom Crypto fork for cryptographic operations

**Audit Deliverables:**
- src/Instrumentation/MemoryMonitor.{h,cpp} (388 lines)
- src/Instrumentation/BootProfiler.{h,cpp} (408 lines)
- docs/MEMORY_AUDIT.md (587 lines)
- .planning/phases/05-synthesis/BACKLOG.md (892 lines)

## Constraints

- **Platform**: ESP32-S3 with limited RAM — every byte matters
- **Framework**: Must work within Arduino/PlatformIO ecosystem
- **Compatibility**: Must maintain LXMF interoperability with Python clients
- **No regressions**: Fixes must not break existing functionality

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Full stack audit | Memory issues could originate anywhere | Good — found issues in all subsystems |
| Prioritized backlog output | Allows tackling issues incrementally | Good — 30 issues with WSJF scores |
| Stability over features | Reliable foundation needed first | Good — clear path forward |
| Data-driven approach | Instrument first, then audit | Good — baseline established |
| WSJF scoring | Prioritize high-impact low-effort | Good — P1 issues clearly identified |
| 5s boot target | Aspirational, config-only | Partial — 5.3s achieved, code changes needed for <5s |

---
*Last updated: 2026-01-24 after v1.1 milestone start*
