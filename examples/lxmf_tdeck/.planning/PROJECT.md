# microReticulum AutoInterface Fix

## What This Is

A milestone to achieve full parity between the C++ AutoInterface implementation and the Python RNS reference implementation. The goal is reliable peer discovery and maintenance on ESP32 devices (T-Deck) communicating with Columba phones and other Reticulum nodes.

## Core Value

**AutoInterface must reliably discover and maintain peer connections, even when multicast is unreliable.**

## Requirements

### Validated

(None yet — ship to validate)

### Active

- [ ] Unicast discovery (reverse peering) for reliable peer maintenance
- [ ] Multicast echo tracking for carrier detection
- [ ] Link-local address change handling
- [ ] Diagnostic logging for network issues

### Out of Scope

- Multi-interface support (ESP32 typically has one WiFi interface) — architectural difference acceptable
- Spawned peer interfaces (C++ uses internal peer list) — architectural difference acceptable

## Context

- **Platform**: ESP32-S3 (T-Deck) with 8MB PSRAM
- **Existing code**: AutoInterface.cpp/h in examples/common/auto_interface/
- **Reference**: Python RNS AutoInterface.py in ~/repos/Reticulum/
- **Issue observed**: Announces not consistently reaching all peers, peers dropping unexpectedly
- **Root cause**: Missing unicast discovery and multicast echo tracking

## Constraints

- **Memory**: ESP32 has limited internal RAM (~80KB free), PSRAM available (~7.5MB)
- **Compatibility**: Must interoperate with Python RNS and Columba apps
- **Protocol**: Discovery tokens and ports must match Python exactly

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Use internal peer list vs spawned interfaces | Simpler for single-interface ESP32 | — Pending |
| PEERING_TIMEOUT = 22s | Match Python RNS exactly | ✓ Good |
| ANNOUNCE_INTERVAL = 1.6s | Match Python RNS exactly | ✓ Good |

---
*Last updated: 2025-01-25 after milestone initialization*
