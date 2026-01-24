# microReticulum Stability Audit

## What This Is

A comprehensive stability audit and fix program for the microReticulum codebase — a C++ implementation of the Reticulum network stack and LXMF messaging protocol for the Lilygo T-Deck Plus and other ESP32-based devices.

## Core Value

Reliable firmware operation for extended periods without crashes or performance degradation.

## Current State (v1.1 Shipped)

**v1.1 Stability Quick Wins — Shipped 2026-01-24**

All 5 P1 stability issues from the v1 audit are now fixed:
- ✅ ODR violation in Persistence module fixed (ArduinoJson 7 API)
- ✅ ResourceData vectors pre-allocated (256 slots)
- ✅ Task Watchdog Timer enabled (10s timeout)
- ✅ LXStamper yield frequency improved 10x
- ✅ BLE callback mutex protection added

**Build status:** Clean (0 warnings), RAM 41.9%, Flash 77.2%

**Next:** Runtime testing on hardware, then P2 issues or new features

## Requirements

### Validated

- Reticulum protocol implementation — existing
- LXMF message exchange with Python clients — existing
- BLE mesh interface with multi-platform support — existing
- LVGL-based UI framework — existing
- FreeRTOS task-based concurrency — existing
- Identity management and cryptography — existing
- Message persistence and routing — existing
- Heap/stack monitoring — v1.0
- Boot sequence profiling — v1.0
- Memory allocation audit — v1.0
- Concurrency pattern audit — v1.0
- Prioritized stability backlog — v1.0
- ✅ MEM-01: Fix ODR violation in Persistence — v1.1
- ✅ MEM-02: Pre-allocate ResourceData vectors — v1.1
- ✅ CONC-01: Enable TWDT for application tasks — v1.1
- ✅ CONC-02: Fix LXStamper yield frequency — v1.1
- ✅ CONC-03: Add mutex to BLE pending queues — v1.1

### Active

None — define requirements for next milestone with `/gsd:new-milestone`

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
- Instrumentation: MemoryMonitor, BootProfiler (v1.0)

**Technical Environment:**
- ESP32-S3 (T-Deck Plus) with 8MB flash, PSRAM
- C++11 with PlatformIO/Arduino framework
- FreeRTOS for task scheduling
- ArduinoJson 7.4.2+, MsgPack 0.4.2+
- Custom Crypto fork for cryptographic operations

**Audit Deliverables (v1.0):**
- src/Instrumentation/MemoryMonitor.{h,cpp} (388 lines)
- src/Instrumentation/BootProfiler.{h,cpp} (408 lines)
- docs/MEMORY_AUDIT.md (587 lines)
- .planning/phases/05-synthesis/BACKLOG.md (892 lines)

**Stability Fixes (v1.1):**
- Persistence ODR compliance with ArduinoJson 7
- ResourceData vector pre-allocation
- TWDT configuration and LVGL integration
- LXStamper yield optimization
- BLE callback mutex protection

## Constraints

- **Platform**: ESP32-S3 with limited RAM — every byte matters
- **Framework**: Must work within Arduino/PlatformIO ecosystem
- **Compatibility**: Must maintain LXMF interoperability with Python clients
- **No regressions**: Fixes must not break existing functionality

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Full stack audit | Memory issues could originate anywhere | ✅ Good — found issues in all subsystems |
| Prioritized backlog output | Allows tackling issues incrementally | ✅ Good — 30 issues with WSJF scores |
| Stability over features | Reliable foundation needed first | ✅ Good — clear path forward |
| Data-driven approach | Instrument first, then audit | ✅ Good — baseline established |
| WSJF scoring | Prioritize high-impact low-effort | ✅ Good — P1 issues clearly identified |
| 5s boot target | Aspirational, config-only | ⚠️ Partial — 5.3s achieved, code changes needed for <5s |
| ArduinoJson 7 API | ODR compliance + elastic allocation | ✅ Good — clean build, future-proof |
| MAX_PARTS = 256 | Based on MAX_EFFICIENT_SIZE / min SDU | ✅ Good — covers practical transfers |
| 10s TWDT timeout | Balance detection vs margin for crypto | ✅ Good — detects hangs, no false triggers |
| 10-round yield frequency | 10x better UI responsiveness | ✅ Good — responsive during stamping |

<details>
<summary>v1.0 Audit Summary</summary>

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

</details>

---
*Last updated: 2026-01-24 after v1.1 milestone complete*
