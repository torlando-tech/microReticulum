# microReticulum Stability Audit

## What This Is

A comprehensive audit of the microReticulum codebase — a C++ implementation of the Reticulum network stack and LXMF messaging protocol for the Lilygo T-Deck Plus and other ESP32-based devices. The firmware is functional (can exchange LXMF messages with Python clients) but exhibits stability issues including slow boot times (15+ seconds) and memory fragmentation leading to crashes during extended runtime.

## Core Value

Identify and prioritize the root causes of instability so the firmware can run reliably for extended periods without crashes or performance degradation.

## Requirements

### Validated

- Reticulum protocol implementation — existing
- LXMF message exchange with Python clients — existing
- BLE mesh interface with multi-platform support — existing
- LVGL-based UI framework — existing
- FreeRTOS task-based concurrency — existing
- Identity management and cryptography — existing
- Message persistence and routing — existing

### Active

- [ ] Memory management audit: allocation patterns, fragmentation sources, potential leaks
- [ ] Boot performance audit: identify blocking operations and initialization bottlenecks
- [ ] Threading/concurrency audit: RTOS task issues, race conditions, mutex contention
- [ ] Prioritized backlog of issues with fix recommendations

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

**Known Symptoms:**
- Boot time exceeds 15 seconds
- Extended runtime (hours/days) leads to crashes
- Memory fragmentation suspected but not yet instrumented

**Prior Investigation:**
- No heap monitoring or stack traces captured yet
- No systematic debugging performed
- This audit is starting fresh

**Technical Environment:**
- ESP32-S3 (T-Deck Plus) with 8MB flash, PSRAM
- C++11 with PlatformIO/Arduino framework
- FreeRTOS for task scheduling
- ArduinoJson 7.4.2+, MsgPack 0.4.2+
- Custom Crypto fork for cryptographic operations

## Constraints

- **Platform**: ESP32-S3 with limited RAM — every byte matters
- **Framework**: Must work within Arduino/PlatformIO ecosystem
- **Compatibility**: Must maintain LXMF interoperability with Python clients
- **No regressions**: Fixes must not break existing functionality

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Full stack audit | Memory issues could originate anywhere | — Pending |
| Prioritized backlog output | Allows tackling issues incrementally | — Pending |
| Stability over features | Reliable foundation needed first | — Pending |

---
*Last updated: 2026-01-23 after initialization*
