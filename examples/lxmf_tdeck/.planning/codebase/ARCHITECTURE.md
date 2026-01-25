# Architecture

**Analysis Date:** 2026-01-24

## Pattern Overview

**Overall:** Layered embedded messaging system with hardware abstraction and event-driven network processing.

**Key Characteristics:**
- Separation of concerns: hardware drivers, networking (Reticulum), messaging (LXMF), and UI (LVGL) operate independently
- Event-driven message processing: asynchronous delivery callbacks and queue-based inbound/outbound message handling
- Modular interface architecture: pluggable network transports (TCP, LoRa, BLE, AutoInterface) with common Interface abstraction
- Dual-core task scheduling: LVGL UI runs on core 1, BLE processing on core 0, main loop on core 0
- Persistent state management: identity stored in NVS (Non-Volatile Storage), messages in SPIFFS filesystem

## Layers

**Hardware Abstraction Layer:**
- Purpose: Encapsulate T-Deck Plus hardware (ESP32-S3, display, keyboard, GPS, radio)
- Location: `src/Hardware/TDeck/` (centralized in parent repo), `examples/lxmf_tdeck/lib/`
- Contains: Config.h for pin mappings, Display, Keyboard, Touch, Trackball drivers, SDLogger for crash debugging
- Depends on: Arduino Core, FreeRTOS, LVGL, RadioLib
- Used by: main setup functions and event handlers in `examples/lxmf_tdeck/src/main.cpp`

**Network Transport Layer:**
- Purpose: Manage multiple network interface abstractions for Reticulum connectivity
- Location: `src/`, `examples/lxmf_tdeck/src/`, `examples/lxmf_tdeck/lib/sx1262_interface/`
- Contains:
  - `Interface.h/cpp` - base class and wrapper
  - `TCPClientInterface` - TCP/WiFi connectivity (custom implementation in example)
  - `SX1262Interface` - LoRa radio interface (custom wrapper around RadioLib)
  - `AutoInterface` - IPv6 peer discovery (from parent repo)
  - `BLEInterface` - Bluetooth mesh connectivity (from parent repo)
- Depends on: Reticulum transport, WiFi, RadioLib, BLE stack
- Used by: `Reticulum` and `Transport` for message routing

**Reticulum Core Layer:**
- Purpose: Handle network packet routing, identity management, and link establishment
- Location: `src/` (parent repo: Reticulum.cpp, Transport.cpp, Identity.cpp, Destination.cpp)
- Contains: packet processing, transport tables, link management, cryptographic operations
- Depends on: Interface abstractions, Cryptography utilities, Filesystem
- Used by: LXMF router, application loop for network processing

**LXMF Messaging Layer:**
- Purpose: High-level message routing, propagation node management, and message persistence
- Location: `src/LXMF/`
- Contains:
  - `LXMRouter` - routes messages between endpoints, handles delivery receipts
  - `LXMessage` - message envelope with metadata (sender, recipient, state)
  - `MessageStore` - persists messages to filesystem
  - `PropagationNodeManager` - discovers and tracks propagation servers
- Depends on: Reticulum, Filesystem
- Used by: UI manager for message display and composition

**Application Logic Layer:**
- Purpose: Bootstrap and orchestrate all subsystems, implement periodic tasks
- Location: `examples/lxmf_tdeck/src/main.cpp`
- Contains: setup functions (hardware, WiFi, Reticulum, LXMF, UI), main loop with timing logic
- Depends on: All other layers
- Provides: GPS time sync, periodic announces, propagation sync, status monitoring

**UI Layer:**
- Purpose: Display messages and settings, handle user input, render interface
- Location: `src/UI/LXMF/`, `examples/lxmf_tdeck/lib/tdeck_ui/`
- Contains: UIManager for message/contact management, SettingsScreen, screens for messaging
- Depends on: LXMF layer, LVGL rendering
- Used by: Application through callbacks for settings changes and message delivery

## Data Flow

**Incoming Message Flow:**

1. Network packet arrives on interface (TCP, LoRa, BLE, or Auto)
2. `Interface::loop()` → `Transport::loop()` in main loop (line 1179)
3. Transport dispatches to registered destination handlers
4. `LXMRouter::process_inbound()` dequeues from inbound queue (line 1199)
5. Router invokes receiver's callback or stores in `MessageStore` via filesystem
6. `UIManager::update()` polls message store for new messages (line 1204)
7. UI renders message and plays notification sound

**Outgoing Message Flow:**

1. User types message in UI
2. UIManager calls `LXMRouter::send()` with recipient and payload
3. Router enqueues message in outbound queue
4. `LXMRouter::process_outbound()` in main loop (line 1198) sends via transport
5. Message transmitted on all online interfaces
6. Destination sends delivery receipt packet back
7. Delivery callback fires (line 1102-1139), updates message state to DELIVERED in store
8. UI updates message display to show "delivered" status

**State Management:**

- **Volatile:** Transport tables (destinations, links, announces), UI state, interface status
- **Persistent:** Identity private key (NVS), messages (MessageStore on SPIFFS), settings (NVS Preferences)
- **GPS State:** Detected models (u-blox, L76K), synchronized time offset via `RNS::Utilities::OS::setTimeOffset()`

## Key Abstractions

**Interface Abstraction:**
- Purpose: Unified interface for multiple transport types
- Examples: `TCPClientInterface`, `SX1262Interface`, `AutoInterface`, `BLEInterface`
- Pattern: Inherit from `RNS::InterfaceImpl`, implement `start()`, `stop()`, `loop()`, `send_outgoing()`
- All register with `Transport::register_interface()` to be polled in main loop

**Message State Machine:**
- `NEW` - incoming, unseen
- `DELIVERED` - confirmed by recipient via receipt
- `FAILED` - transmission failed after retries
- Persisted in MessageStore, updated via `update_message_state()` and delivery callbacks

**Settings Abstraction:**
- Purpose: Centralize app configuration with callback-driven runtime changes
- Examples: WiFi credentials, LoRa parameters, brightness, notification settings
- Pattern: Loaded from NVS on boot into `UI::LXMF::AppSettings`, updated via settings screen callback (line 836-1022)
- Changes trigger interface reinitialization, display reconfiguration, or propagation node updates

**Propagation Node Manager:**
- Purpose: Auto-discover and cache propagation nodes for message relay
- Pattern: Listens to announces, tracks reachability, selects best node
- Usage: Enables offline message sending - router caches locally until connection available, then forwards via propagation server

## Entry Points

**Application Entry:**
- Location: `examples/lxmf_tdeck/src/main.cpp`
- Triggers: ESP32 boot
- Responsibilities: Initialize hardware, load identity, start networking, launch UI

**Main Loop:**
- Location: `examples/lxmf_tdeck/src/main.cpp::loop()` (lines 1160-1437)
- Triggers: Called repeatedly by Arduino framework (typically 5-10ms interval due to delay(5) at line 1436)
- Responsibilities: Poll all interfaces, process transport/LXMF queues, update UI, handle periodic announcements, monitor memory/heap

**Message Delivery Event:**
- Location: `examples/lxmf_tdeck/src/main.cpp` line 1102 - registered callback
- Triggers: When delivery receipt arrives from recipient
- Responsibilities: Update message state in store, reload full message with destination, notify UI

**Settings Change Event:**
- Location: `examples/lxmf_tdeck/src/main.cpp` line 836 - SettingsScreen callback
- Triggers: User saves settings screen
- Responsibilities: Apply WiFi changes, restart interfaces (TCP/LoRa/Auto/BLE), update router configuration

## Error Handling

**Strategy:** Defensive with graceful degradation - missing components don't crash application, interfaces fail over safely.

**Patterns:**

- **Interface Failures:** If TCP fails to connect, system continues with LoRa/BLE/Auto; each interface independently logs errors and retries (line 592-602, 621-626)
- **Time Sync Failures:** GPS time sync times out after 15s (line 1064), falls back to NTP via WiFi (line 434); if both fail, system continues with unsynced time
- **Settings Load Failures:** Missing NVS data loads sensible defaults (line 353-389); invalid settings don't prevent boot
- **FileSystem Failures:** If SPIFFS mount fails, MessageStore cannot persist but system continues (line 476-480); restart recommends filesystem reset
- **Memory Exhaustion:** Heap monitor logs warnings at 50KB free, critical at 20KB; can trigger table diagnostics (line 1387-1430)

## Cross-Cutting Concerns

**Logging:**
- Framework: Arduino Serial with INFO/WARNING/ERROR/DEBUG macros
- Bootstrap profiling: `BOOT_PROFILE_*` macros wrap setup functions to measure timing (enabled via flag, line 70)
- Memory instrumentation: `MemoryMonitor` logs heap/stack/PSRAM periodically (enabled via flag, line 65, 520-533)

**Validation:**
- Network: TCP keepalive and stale connection detection (line 1254), LoRa packet CRC validated by RadioLib
- Message: LXMF router validates sender identity via Reticulum receipts
- Settings: Each field has type-safe getter with default fallback (line 353-389)

**Authentication:**
- Network: Reticulum identity system provides cryptographic proof of sender (no passwords)
- UI Settings: No access control (single-user embedded device)

---

*Architecture analysis: 2026-01-24*
