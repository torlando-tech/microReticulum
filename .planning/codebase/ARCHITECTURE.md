# Architecture

**Analysis Date:** 2026-01-23

## Pattern Overview

**Overall:** Layered C++ Library Architecture with Object-Wrapper Pattern

**Key Characteristics:**
- Implicit object sharing using shared pointers and wrapper classes
- Clean separation between public API and implementation details
- Port of Python Reticulum Network Stack to C++ for MCUs
- Memory-efficient pool-based collections to reduce heap fragmentation
- Plugin-based interface architecture for hardware abstraction
- FreeRTOS task-based concurrency for responsive UI and networking

## Layers

**Core Networking Layer:**
- Purpose: Implements Reticulum network protocol for mesh routing and communication
- Location: `src/Transport.h`, `src/Transport.cpp`, `src/Packet.h`, `src/Packet.cpp`
- Contains: Packet routing, transport management, path finding, destination tracking, rate limiting
- Depends on: Interface, Destination, Identity, Cryptography
- Used by: Application layer, LXMF

**Cryptography Layer:**
- Purpose: Provides encryption, signing, key exchange, and hash functions
- Location: `src/Cryptography/`
- Contains: Ed25519, X25519, AES, CBC, Fernet, HKDF, HMAC, Token, Ratchet, Hashes, BZ2
- Depends on: External crypto libraries (rweather/Crypto, etc.)
- Used by: Identity, Link, Destination, Transport

**Identity & Destination Layer:**
- Purpose: Manages cryptographic identities, announcements, and network endpoints
- Location: `src/Identity.h`, `src/Identity.cpp`, `src/Destination.h`, `src/Destination.cpp`
- Contains: Public/private key management, destination creation and registration, announce handler callbacks, known destination caching
- Depends on: Cryptography layer
- Used by: Transport, Link, Packet, Interface

**Link & Channel Layer:**
- Purpose: Implements encrypted bidirectional communication channels and resource transfer
- Location: `src/Link.h`, `src/Link.cpp`, `src/Channel.h`, `src/Channel.cpp`, `src/Resource.h`, `src/Resource.cpp`
- Contains: Link establishment, resource advertisement and transfer, request/response mechanics, packet receipts
- Depends on: Packet, Destination, Transport, Cryptography
- Used by: Transport, Application protocols

**Interface Layer:**
- Purpose: Hardware abstraction for physical communication mediums
- Location: `src/Interface.h`, `src/Interface.cpp`, `src/BLE/`, Hardware drivers
- Contains: Base interface classes, BLE implementation with platform abstraction (NimBLE, Bluedroid), incoming/outgoing data handlers
- Depends on: Packet, Reticulum (for transport hooks)
- Used by: Transport, Reticulum

**BLE Subsystem:**
- Purpose: Bluetooth Low Energy mesh interface with multi-platform support
- Location: `src/BLE/`
- Contains: `BLEPlatform.h` (HAL), platform implementations (NimBLE, Bluedroid), peer management, identity management, fragmentation, reassembly
- Depends on: Interface abstraction, platform-specific BLE stacks
- Used by: Reticulum as configurable interface

**Message Layer (LXMF):**
- Purpose: Implements LXMF (Libertarian eXtensible Message Format) for messaging
- Location: `src/LXMF/`
- Contains: Message routing, message store (persistence), propagation node management, stamping
- Depends on: Link, Transport, Destination
- Used by: Application (lxmf_tdeck example)

**UI Layer:**
- Purpose: LVGL-based UI framework with application-specific screens
- Location: `src/UI/LVGL/`, `src/UI/LXMF/`
- Contains: LVGL initialization and locking, LXMF-specific screens (chat, compose, settings, QR codes, announce lists)
- Depends on: LXMF, Hardware (T-Deck)
- Used by: Applications

**Hardware Abstraction Layer:**
- Purpose: Device-specific drivers and configurations
- Location: `src/Hardware/TDeck/`
- Contains: Display drivers, keyboard input, touch handling, trackball, device configuration
- Depends on: Arduino framework, device-specific libraries
- Used by: Applications

**Utilities & Infrastructure:**
- Purpose: Common utilities and helper functions
- Location: `src/Utilities/`, `src/Persistence.h`
- Contains: Byte stream handling, OS abstraction, file system interface, memory management (TLSF), JSON serialization via ArduinoJson
- Depends on: Standard libraries
- Used by: All layers

## Data Flow

**Outgoing Packet Flow:**

1. Application creates Destination and calls send on Packet
2. Packet.send() → Transport.send()
3. Transport.send() selects next hop interface and routes via path table
4. Interface.handle_outgoing() → physical send_outgoing() (BLE/LoRa/UDP)
5. PacketReceipt returned to caller
6. Transport tracks packet for proof validation

**Incoming Packet Flow:**

1. Interface receives data → handle_incoming()
2. Transport.receive_packet() processes packet
3. Transport verifies destination, checks for local handling
4. If local: calls registered packet callbacks or Link handlers
5. If not local: routes via next hop (if transport enabled)
6. Proof packets update PacketReceipt delivery status

**Link Establishment Flow:**

1. Application calls Destination.create_link(target_hash)
2. Link negotiates encryption via exchange of public keys
3. Link moves to OPEN state with shared session key
4. Application can send/receive via Link.send()

**Message (LXMF) Flow:**

1. Application sends message via LXMRouter.send()
2. Message is stored in MessageStore (filesystem)
3. Router creates Link to destination if needed
4. Message transferred via Resource or Link
5. Propagation nodes can relay messages

**State Management:**

- **Transport state**: Global pools for interfaces, destinations, routes, rate limits
- **Identity state**: Fixed-size pool for known destinations to reduce heap fragmentation
- **Link state**: Per-link encryption context, ratchet state for forward secrecy
- **Interface state**: Per-interface TX/RX byte counters, announce queues, connection lists
- **Persistence**: Configuration, identities, routing tables saved to filesystem

## Key Abstractions

**Wrapper Pattern (Implicit Object Sharing):**
- Purpose: Enables safe shared ownership and copy semantics
- Examples: `Packet`, `Identity`, `Destination`, `Link`, `PacketReceipt`, `RequestReceipt`
- Pattern: Public wrapper class holds `shared_ptr<Object>` private data; copy constructor copies pointer; destructor via smart pointer
- Benefit: Automatic lifetime management, thread-safe reference counting

**Interface Plugin Pattern:**
- Purpose: Swap communication backends without changing Transport logic
- Examples: `Interface`, `InterfaceImpl`, `BLEPlatform`, `IBLEPlatform`
- Pattern: Abstract base class `InterfaceImpl`; Transport calls virtual send/loop/start methods
- Benefit: Runtime selection of BLE vs LoRa vs UDP without compile-time coupling

**Handler Callbacks:**
- Purpose: Decouple application logic from framework
- Examples: `AnnounceHandler`, `RequestHandler`, packet callbacks
- Pattern: Virtual method or function pointer callback registered with Destination/Link
- Benefit: Application defines handlers without modifying framework

**Type Constants (Namespace):**
- Purpose: Configure protocol limits and defaults without hardcoding
- Location: `src/Type.h`
- Examples: MTU=500, HEADER sizes, timeouts, hash lengths
- Benefit: Single source of truth for network constants, easy tuning

**Persistence Layer:**
- Purpose: Abstract filesystem differences across platforms
- Location: `src/Persistence.h`, `src/FileSystem.h`
- Pattern: JSON serialization via ArduinoJson, files stored in hierarchical structure
- Benefit: Can swap filesystem backends (SPIFFS, LittleFS, SD card)

## Entry Points

**Reticulum Singleton (Global):**
- Location: `src/Reticulum.h`
- Triggers: Instantiated once at application startup
- Responsibilities: Initializes Transport, manages interfaces, handles networking loops, persists data

**main.cpp:**
- Location: `src/main.cpp`, example in `examples/lxmf_tdeck/src/main.cpp`
- Triggers: Arduino setup()/loop() or standard C++ main()
- Responsibilities: Application initialization, UI setup, calling reticulum.loop()

**Example Application (lxmf_tdeck):**
- Location: `examples/lxmf_tdeck/src/main.cpp`
- Triggers: PlatformIO upload to T-Deck hardware
- Responsibilities: Creates Reticulum, initializes LXMF router, sets up UI screens, manages message sync

## Error Handling

**Strategy:** Exceptions are minimal; return codes and validity checks dominate

**Patterns:**
- Assertion checks in debug build for invalid usage (`assert(_object)`)
- Boolean return values for success/failure of operations (e.g., `Identity::create()`)
- Validity operator `operator bool()` on wrapper objects to check if data is null
- Logging via `Log.h` macros for warnings and errors
- Timeout-based recovery for network failures (Link timeout, path expiry)

## Cross-Cutting Concerns

**Logging:**
- Macros in `src/Log.h`: `LOG()`, `LOGF()`, `MEM()`, `MEMF()` for memory tracking
- Levels controlled via compile-time defines
- Memory tracking enabled for debugging heap fragmentation

**Validation:**
- Cryptographic signature verification on all packets
- Destination hash validation before processing
- Type checking via wrapper object `operator bool()`

**Authentication:**
- Ed25519 signatures on all packets
- Identity verification via public key lookup
- Link encryption with forward secrecy via Ratchet

**Task Scheduling:**
- FreeRTOS tasks for UI (LVGL) and BLE on dedicated cores
- Main task handles Transport loops and interface housekeeping
- Job queue in Transport for delayed operations (link timeouts, path expiry)

---

*Architecture analysis: 2026-01-23*
