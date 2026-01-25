# External Integrations

**Analysis Date:** 2026-01-24

## APIs & External Services

**Reticulum Network:**
- Reticulum (microReticulum) - Mesh networking framework providing packet routing, identity management, and transport abstraction
  - SDK/Client: Linked from parent repository (`src/Reticulum.h`, `src/Identity.h`, `src/Destination.h`)
  - Communication: Uses multiple transport interfaces (LoRa, BLE, WiFi, TCP)

**LXMF (Lightweight eXtendable Messaging Format):**
- Message routing and delivery protocol built on Reticulum
  - Implementation: `src/LXMF/LXMRouter.h`, `src/LXMF/LXMessage.h`
  - Propagation Sync: `src/LXMF/PropagationNodeManager.h` for syncing via propagation nodes
  - Message delivery callbacks: DeliveryCallback, SentCallback, DeliveredCallback, FailedCallback

**GPS Time & Location Services:**
- TinyGPSPlus GPS parsing library - Provides time synchronization and location data
  - Module: L76K/L76B connected to UART1 on T-Deck Plus
  - Usage in `src/main.cpp`: Time sync with timezone calculation, location data for UI display

## Data Storage

**Databases:**
- Filesystem-based (JSON) - No traditional database
  - Storage location: SPIFFS partition (1.9MB allocated in `partitions.csv` at offset 0x610000)
  - Client: UniversalFileSystem abstraction (`lib/universal_filesystem`)

**File Storage:**
- SPIFFS (SPI Flash File System) - Internal flash storage for message data and configuration
  - Message storage: JSON files organized by conversation hash (`messages/<hash>.json`)
  - Conversation index: `conversations.json`
  - User preferences: NVS (Non-Volatile Storage) via Arduino `Preferences` class
  - Application settings: Loaded into `UI::LXMF::AppSettings` in `src/main.cpp:83`

**SD Card:**
- MicroSD card slot available on T-Deck Plus
  - Used for: Crash debugging logs via SDLogger (`Hardware/TDeck/SDLogger.h`)
  - Mounted as secondary filesystem when present
  - Write location: SD card paths for persistent debug logs

**Caching:**
- Memory-based message pools:
  - Known destinations pool (192 slots) - RNode peer registry
  - BytesPool for buffer management (tiny tier: 512 slots post-optimization)
  - Conversation index caching in memory from SPIFFS
- No external caching service

## Authentication & Identity

**Auth Provider:**
- Custom (Reticulum Identity System)
  - Implementation: `src/Identity.h`, cryptographic identity management built into Reticulum
  - Approach: Ed25519 digital signatures for node authentication and message authenticity
  - Key derivation: HKDF used for shared secret establishment between peers
  - Stored in: SPIFFS as binary identity files

**Bluetooth Identity:**
- NimBLE device identity per BLEInterface instance
  - Local identity hash registered in `BLEInterface` (`examples/common/ble_interface/BLEInterface.h`)
  - Peer identity tracking via BLEPeerManager (`src/BLE/BLEPeerManager.h`)
  - Maximum 10 bonded peers (configured via `CONFIG_BT_NIMBLE_MAX_BONDS=10`)

## Monitoring & Observability

**Error Tracking:**
- SD Card logging - Crash dumps and debug traces via SDLogger
  - Location: SD card (if present) for persistent error logs
  - Enabled via flag `-DMEMORY_INSTRUMENTATION_ENABLED` (optional)

**Logs:**
- Serial logging (USB CDC) at 115200 baud
  - Log level: WARNING (CORE_DEBUG_LEVEL=2) to reduce verbosity during boot
  - Macros: `INFO()`, `WARNING()`, `ERROR()` via Log.h
  - Stack traces: ESP32 exception decoder filter in PlatformIO monitor

**Performance Monitoring:**
- Memory instrumentation (optional): `MEMORY_INSTRUMENTATION_ENABLED` flag
  - Tracks heap/stack usage via Instrumentation/MemoryMonitor.h
  - Disabled by default to reduce overhead
- Boot profiling (optional): `BOOT_PROFILING_ENABLED` flag
  - Timing for setup phases via Instrumentation/BootProfiler.h
  - Disabled by default

**Task Monitoring:**
- FreeRTOS Task Watchdog Timer (TWDT)
  - Configuration: 10 second timeout with idle task checking on both CPU cores
  - Detects task starvation and deadlock conditions

## CI/CD & Deployment

**Hosting:**
- Embedded firmware on ESP32-S3 (T-Deck Plus hardware)
- OTA (Over-The-Air) update capable via partition table (app0/app1 slots in `partitions.csv`)

**CI Pipeline:**
- Not detected - Build performed locally via PlatformIO

**Firmware Upload:**
- USB CDC-ACM connection (native USB) via `-DARDUINO_USB_CDC_ON_BOOT=1`
- PlatformIO upload via USB serial

**Web Flasher Support:**
- Firmware version constant defined: `#define FIRMWARE_VERSION "1.0.0"`
- Firmware name: `FIRMWARE_NAME "microReticulum"` for web flasher detection

## Environment Configuration

**Required env vars:**
- Not detected - Configuration is ROM-based (NVS) and settings file-based
- GPS time uses environment variable TZ (timezone) set from GPS location at runtime

**Secrets location:**
- Reticulum identities stored as binary files in SPIFFS (not encrypted by default)
- BLE bonds (paired devices) stored in NVS (non-volatile storage) - `CONFIG_BT_NIMBLE_MAX_BONDS=10`

## Transport Interfaces

**LoRa (SX1262):**
- Modem: Semtech SX1262 connected via SPI
  - Interface: `lib/sx1262_interface/SX1262Interface.h`
  - Frequency: 927.25 MHz (configurable)
  - Spreading factor: SF7-SF12 (configurable)
  - Bandwidth: 62.5 kHz (configurable)
  - TX Power: 17 dBm (configurable)
  - RNode protocol compatible for air interoperability
  - Driver: RadioLib v6.0

**Bluetooth LE Mesh:**
- BLE stack: NimBLE-Arduino v2.1.0 (default) or Bluedroid (fallback)
  - Interface: `examples/common/ble_interface/BLEInterface.h`
  - Protocol: BLE-Reticulum v2.2
  - Dual-mode: Central + Peripheral for mesh networking
  - Max connections: 3 simultaneous peers
  - MTU: 517 bytes (negotiated)
  - Fragmentation/reassembly: BLEFragmenter, BLEReassembler (`src/BLE/`)

**WiFi (TCP/UDP):**
- TCP Client Interface: `src/TCPClientInterface.h`
  - Connects to local WiFi network
  - Used for remote peer connections via TCP
  - Enabled via `app_settings.tcp_enabled` (user configurable)

**Auto Interface (IPv6 Peer Discovery):**
- Network peer discovery via IPv6 multicast
  - Interface: `examples/common/auto_interface/AutoInterface.h`
  - Discovers peers on same local network automatically
  - Enabled via `app_settings.auto_enabled` (user configurable)

## Webhooks & Callbacks

**Incoming:**
- LXMF message delivery callbacks registered in LXMRouter (`src/LXMF/LXMRouter.h`):
  - `DeliveryCallback` - For received messages
  - `SyncCompleteCallback` - When propagation sync finishes
- TCP Client callbacks for connection state changes

**Outgoing:**
- LXMF message delivery confirmations:
  - `SentCallback` - When message sent to peer
  - `DeliveredCallback` - When peer confirms receipt
  - `FailedCallback` - When delivery fails
- User notifications triggered via audio tone generator (`lib/tone/Tone.h`)

---

*Integration audit: 2026-01-24*
