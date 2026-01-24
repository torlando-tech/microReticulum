# External Integrations

**Analysis Date:** 2026-01-23

## APIs & External Services

**Reticulum Network Stack:**
- Reticulum Network Stack - Open mesh networking protocol
  - Reference implementation: Python Reticulum (https://reticulum.network/)
  - This codebase provides C++ port for embedded systems
  - No external API calls; implements the protocol locally

**No External Web APIs:**
- This is a network stack library, not a service consumer
- Designed for offline-first mesh networking (no cloud dependencies)

## Data Storage

**Databases:**
- None - No database integration
- Persistent state stored locally via file system

**File Storage:**
- Local filesystem only (when `RNS_USE_FS` enabled)
- Storage paths configurable via:
  - `Reticulum::_storagepath` - Runtime data (routing tables, identities)
  - `Reticulum::_cachepath` - Temporary cached data
- Persistence managed by `src/Utilities/Persistence.cpp/h`
- Data format: Custom binary serialization via MsgPack

**Caching:**
- In-memory caching via maps/vectors
- Path table cache: `Transport.cpp` manages routing cache
- Rate limiting cache: `Transport.cpp` rate table
- No external cache service (Redis, memcached, etc.)

## Authentication & Identity

**Auth Provider:**
- Custom - Reticulum native identity system
  - Implementation: `src/Identity.cpp/h`
  - Key exchange: X25519 (ECDH)
  - Signature: Ed25519
  - Identity persistence: File system storage via Persistence layer
  - MAC rotation support: `src/BLE/BLEIdentityManager.cpp/h` for Protocol v2.2

**Identity Management:**
- `src/Destination.cpp/h` - Named endpoints authenticated by identity
- `src/BLE/BLEIdentityManager.cpp/h` - BLE-specific identity and peer management
- Implicit proof vs. explicit proof modes configurable at `Reticulum` level
- Remote management support via authenticated peers (`Reticulum::remote_management_enabled()`)

## BLE (Bluetooth Low Energy)

**Primary BLE Implementation:**
- NimBLE-Arduino (implicit dependency on ESP32 boards with `USE_NIMBLE=1`)
  - Platform: `src/BLE/platforms/NimBLEPlatform.cpp/h`
  - Interface: `src/BLE/BLEPlatform.h` (abstraction layer)
  - Custom operation queue: `src/BLE/BLEOperationQueue.cpp/h`
  - Peer management: `src/BLE/BLEPeerManager.cpp/h`
  - Fragmentation/reassembly: `src/BLE/BLEFragmenter.cpp/h`, `src/BLE/BLEReassembler.cpp/h`
  - Example integration: `examples/common/ble_interface/BLEInterface.h`

**BLE Protocol Details:**
- Reticulum BLE-Protocol v2.2
- Dual-mode operation: Central (scanner/connector) and Peripheral (advertiser) simultaneously
- Service UUID and characteristics for RX/TX data paths
- Identity characteristic for peer discovery
- MTU negotiation for connection optimization
- MAC rotation for privacy and MAC spoofing mitigation
- Scan interval: 5.0 seconds (configurable)
- State machine-based connection management with error recovery

**Alternative BLE Implementation (Not Active):**
- BluedroidPlatform (`src/BLE/platforms/BluedroidPlatform.h`) - Fallback for Bluedroid stack
  - Documented as workaround for NimBLE state machine bugs

## Wireless/Communication Interfaces

**LoRa (Long Range Radio):**
- Example implementation: `examples/common/lora_interface/LoRaInterface.cpp/h`
- Not integrated into core library, provided as reference example
- Custom interface abstraction via `src/Interface.h/cpp`

**UDP Interface:**
- Example implementation: `examples/common/udp_interface/UDPInterface.cpp/h`
- For testing/development on native platforms
- Allows testing Reticulum against Python reference implementation

**Auto Interface:**
- Example implementation: `examples/common/auto_interface/AutoInterface.cpp/h`
- Automatic peer discovery and connection

## Display/UI Integration

**OLED Display (T-Beam Supreme):**
- Hardware: SH1106 OLED controller
- Driver: Adafruit SH110X 2.1.10+
- Graphics: Adafruit GFX Library 1.11.9+
- LVGL Integration: `src/UI/LVGL/LVGLInit.cpp/h`
  - FreeRTOS task support for dedicated UI rendering
  - Thread-safe access via mutex: `LVGLInit::get_mutex()`
  - Display dimensions configurable via `DISPLAY_SDA`/`DISPLAY_SCL` pins (17/18 for T-Beam Supreme)

**UI Screens:**
- Status display: `src/UI/LXMF/StatusScreen.cpp/h` - Node identity, interface, network stats
- Conversation list: `src/UI/LXMF/ConversationListScreen.cpp/h`
- Settings screen: `src/UI/LXMF/SettingsScreen.cpp/h`
- UI manager: `src/UI/LXMF/UIManager.cpp/h`
- Clipboard support: `src/UI/Clipboard.cpp/h`

## Hardware Integration

**GPIO/Serial:**
- Serial communication via Arduino framework (115200 baud default)
- Pin configuration for display (SDA=17, SCL=18 on T-Beam Supreme)
- No direct hardware abstraction layer; uses Arduino and FreeRTOS APIs

**Timekeeping:**
- `Utilities::OS::time()` - Provides system time abstraction (`src/Utilities/OS.cpp/h`)
- Used for persistence scheduling and rate limiting

## Monitoring & Observability

**Error Tracking:**
- None - No external error tracking service
- Logging via `src/Log.cpp/h` to console/UART

**Logs:**
- Serial UART output (console logging)
- Compile-time logging levels (DEBUG, INFO, WARNING, ERROR)
- No file-based logging to disk (memory-constrained devices)

## Network Topology

**Peer Discovery:**
- BLE advertising: Broadcast identity, support capabilities
- Active scanning: 5.0 second intervals to find peers
- Peer table: `src/BLE/BLEPeerManager.cpp/h` tracks connected peers
- Path discovery: Transport layer mechanism via path requests

**Mesh Networking:**
- Multi-hop relay via Transport layer: `src/Transport.cpp/h`
- Path table caching with TTL-based expiration
- Announce mechanism for destination discovery
- Rate limiting to prevent flooding

## Configuration Files

**No External Config Services:**
- Configuration hardcoded at compile-time via platformio.ini build flags
- Runtime configuration via C++ API (no external config server)

**Required Build-Time Env Vars:**
- Device-specific: `BOARD_TBEAM_SUPREME`, `BOARD_ESP32`, etc.
- Feature flags: `HAS_DISPLAY`, `USE_NIMBLE`, `DISPLAY_TYPE_SH1106`
- Storage: `RNS_USE_FS`, `RNS_PERSIST_PATHS`
- Memory: `RNS_USE_ALLOCATOR`, `RNS_USE_TLSF`

## Webhooks & Callbacks

**Incoming:**
- None - This is a library/stack, not a service with webhooks

**Outgoing:**
- Callback-based architecture for BLE events: `src/BLE/BLEPlatform.h`
  - OnScanResult, OnScanComplete
  - OnConnected, OnDisconnected, OnMTUChanged
  - OnDataReceived, OnWriteReceived, OnReadRequested
  - OnNotifyEnabled, OnServicesDiscovered
- Callback-based for Destination lifecycle events
- Callback-based for Transport events

## Dependencies on External Code

**Embedded via Git:**
- Crypto library: `https://github.com/attermann/Crypto.git` (fork of rweather/arduinolibs)
  - Includes cryptographic primitives for Ed25519, X25519, AES, HKDF, HMAC, SHA256

**External Repositories (not embedded):**
- NimBLE-Arduino - Fetched by ESP32 platform at build time
- ArduinoJson - Fetched by PlatformIO at build time
- MsgPack - Fetched by PlatformIO at build time
- Adafruit SH110X/GFX - Fetched for display boards at build time

---

*Integration audit: 2026-01-23*
