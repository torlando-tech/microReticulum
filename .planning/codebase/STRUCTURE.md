# Codebase Structure

**Analysis Date:** 2026-01-23

## Directory Layout

```
microReticulum/
├── src/                              # Main library implementation
│   ├── Transport.h/cpp               # Core mesh routing and packet forwarding
│   ├── Reticulum.h/cpp               # Main singleton coordinator
│   ├── Packet.h/cpp                  # Packet structure and operations
│   ├── Link.h/cpp                    # Encrypted bidirectional links
│   ├── Destination.h/cpp             # Network endpoints and handlers
│   ├── Identity.h/cpp                # Cryptographic identities
│   ├── Interface.h/cpp               # Physical medium abstraction
│   ├── Channel.h/cpp                 # Streaming channels over links
│   ├── Resource.h/cpp                # Large file transfers
│   ├── Display.h/cpp                 # Status display management
│   ├── Bytes.h/cpp                   # Binary data container
│   ├── Buffer.h/cpp                  # Stream buffers and readers
│   ├── Type.h                        # Protocol constants and type definitions
│   ├── Log.h/cpp                     # Logging framework
│   ├── FileStream.h                  # File I/O abstraction
│   ├── FileSystem.h                  # Filesystem interface
│   ├── SegmentAccumulator.h/cpp       # Packet reassembly
│   ├── ChannelData.h                 # Channel internal structures
│   ├── LinkData.h                    # Link internal structures
│   ├── ResourceData.h                # Resource internal structures
│   ├── MessageBase.h                 # Base for streaming messages
│   ├── main.cpp                      # Library entry point (stub)
│   │
│   ├── Cryptography/                 # Encryption and signing
│   │   ├── Ed25519.h/cpp             # Public key signatures
│   │   ├── X25519.h/cpp              # Key exchange
│   │   ├── AES.h                     # AES encryption (header only)
│   │   ├── CBC.h/cpp                 # AES-CBC mode
│   │   ├── Fernet.h/cpp              # Fernet authenticated encryption
│   │   ├── HKDF.h/cpp                # HMAC-based key derivation
│   │   ├── HMAC.h                    # HMAC authentication
│   │   ├── PKCS7.h                   # PKCS7 padding
│   │   ├── Hashes.h/cpp              # SHA256, MD5
│   │   ├── Random.h                  # Random number generation
│   │   ├── Token.h/cpp               # Token encryption wrapper
│   │   ├── Ratchet.h/cpp             # Forward secrecy ratcheting
│   │   └── BZ2.h/cpp                 # BZ2 compression
│   │
│   ├── BLE/                          # Bluetooth Low Energy interface
│   │   ├── BLEPlatform.h             # Hardware abstraction interface
│   │   ├── BLEPlatform.cpp           # Platform factory
│   │   ├── BLETypes.h                # BLE protocol types
│   │   ├── BLEFragmenter.h/cpp       # Packet fragmentation
│   │   ├── BLEReassembler.h/cpp      # Packet reassembly
│   │   ├── BLEOperationQueue.h/cpp   # GATT operation queuing
│   │   ├── BLEIdentityManager.h/cpp  # Identity persistence
│   │   ├── BLEPeerManager.h/cpp      # Peer tracking
│   │   └── platforms/                # Platform-specific implementations
│   │       ├── NimBLEPlatform.h/cpp  # NimBLE stack
│   │       └── BluedroidPlatform.h/cpp # Bluedroid stack
│   │
│   ├── LXMF/                         # Libertarian eXtensible Message Format
│   │   ├── LXMRouter.h/cpp           # Message routing engine
│   │   ├── LXMessage.h/cpp           # Message structure
│   │   ├── MessageStore.h/cpp        # Message persistence
│   │   ├── PropagationNodeManager.h/cpp # Propagation relay management
│   │   ├── LXStamper.h/cpp           # Message signing and verification
│   │   └── Type.h                    # LXMF type definitions
│   │
│   ├── UI/                           # User interface framework
│   │   ├── Clipboard.h               # Clipboard abstraction
│   │   ├── LVGL/                     # LVGL graphics library
│   │   │   ├── LVGLInit.h            # LVGL initialization
│   │   │   └── LVGLLock.h            # Thread-safe LVGL access
│   │   └── LXMF/                     # LXMF-specific UI screens
│   │       ├── UIManager.h/cpp       # Screen manager
│   │       ├── ChatScreen.h          # Message chat interface
│   │       ├── ComposeScreen.h       # Message composition
│   │       ├── ConversationListScreen.h # Inbox
│   │       ├── AnnounceListScreen.h  # Network announces
│   │       ├── PropagationNodesScreen.h # Message relays
│   │       ├── SettingsScreen.h      # Application settings
│   │       ├── QRScreen.h            # QR code display
│   │       └── StatusScreen.h        # Connection status
│   │
│   ├── Hardware/                     # Device-specific drivers
│   │   └── TDeck/                    # LilyGO T-Deck Plus device
│   │       ├── Config.h              # Device pin/resource configuration
│   │       ├── Display.h/cpp         # Display control (ST7789)
│   │       ├── Keyboard.h            # Keyboard matrix input
│   │       ├── Touch.h               # Capacitive touch input
│   │       └── Trackball.h           # Trackball input
│   │
│   └── Utilities/                    # Common infrastructure
│       ├── OS.h/cpp                  # OS abstraction (timing, sleeping)
│       ├── Stream.h/cpp              # Stream I/O base class
│       ├── Print.h/cpp               # Print formatting base class
│       ├── Persistence.h/cpp         # JSON serialization and storage
│       ├── Crc.h/cpp                 # CRC calculation
│       └── tlsf.h                    # TLSF memory allocator
│
├── examples/                         # Example applications
│   ├── lxmf_tdeck/                   # T-Deck LXMF messenger application
│   │   ├── src/main.cpp              # Application entry point
│   │   ├── src/TCPClientInterface.h/cpp # TCP interface for desktop testing
│   │   ├── src/SX1262Interface.h     # LoRa radio interface
│   │   ├── src/AutoInterface.h       # IPv6 peer discovery interface
│   │   ├── src/BLEInterface.h        # BLE Mesh interface
│   │   ├── platformio.ini            # Build configuration
│   │   └── lib/                      # Example-specific libraries
│   │
│   ├── transport_node_tbeam_supreme/ # T-Beam Supreme transport node
│   │   └── src/main.cpp
│   │
│   ├── udp_announce/                 # UDP interface announce example
│   │   └── src/main.cpp
│   │
│   ├── lora_announce/                # LoRa interface announce example
│   │   └── src/main.cpp
│   │
│   ├── link/                         # Point-to-point link example
│   │   └── src/main.cpp
│   │
│   └── common/                       # Shared example code
│
├── test/                             # Unit tests
│   ├── test_bytes/                   # Bytes container tests
│   ├── test_crypto/                  # Cryptography tests
│   ├── test_collections/             # Pool and collection tests
│   ├── test_filesystem/              # Persistence tests
│   ├── test_msgpack/                 # MessagePack serialization tests
│   ├── test_objects/                 # Object wrapper pattern tests
│   ├── test_general/                 # General functionality
│   ├── test_example/                 # Example application tests
│   ├── common/                       # Shared test utilities
│   └── README                        # Test documentation
│
├── docs/                             # Documentation
│   └── Various architecture and protocol docs
│
├── .github/                          # GitHub configuration
│
├── .pio/                             # PlatformIO caches
│
├── .planning/                        # GSD planning documents
│   └── codebase/
│       ├── ARCHITECTURE.md
│       └── STRUCTURE.md
│
├── CMakeLists.txt                    # CMake build configuration
├── platformio.ini                    # PlatformIO environments
├── library.json/properties           # Library metadata
├── README.md                         # Project overview
├── MICRORETICULUM_PROJECT_BOOTSTRAP.md # Setup guide
└── .gitignore                        # Git ignore rules
```

## Directory Purposes

**src/:**
- Purpose: Main library implementation following layered architecture
- Contains: C++ header/implementation pairs for all network and UI functionality
- Key files: `Transport.h` (5214 lines, largest), `Link.cpp`, `Resource.cpp`, `Identity.cpp`

**src/Cryptography/:**
- Purpose: Cryptographic operations (encryption, signing, key exchange)
- Contains: Ed25519/X25519 wrappers, AES modes, Fernet, HKDF, hashing, compression
- Key files: Standalone implementations and wrappers around external crypto libraries

**src/BLE/:**
- Purpose: Bluetooth Low Energy mesh interface with platform abstraction
- Contains: HAL interface, platform implementations (NimBLE/Bluedroid), peer/identity management
- Key files: `BLEPlatform.h` (interface), platform/NimBLEPlatform.cpp (largest implementation)

**src/LXMF/:**
- Purpose: LXMF messaging protocol and message routing
- Contains: Message structure, router logic, message store, propagation management
- Key files: `LXMRouter.cpp` (53KB, core routing), `LXMessage.cpp`, `MessageStore.cpp`

**src/UI/:**
- Purpose: User interface framework and application screens
- Contains: LVGL integration, LXMF-specific screens (chat, compose, settings, QR)
- Key files: Application-specific screens under UI/LXMF/

**src/Hardware/TDeck/:**
- Purpose: LilyGO T-Deck Plus device drivers
- Contains: Display (ST7789), keyboard matrix, touch, trackball, pin configuration
- Key files: Each major input/output has dedicated H/CPP pair

**src/Utilities/:**
- Purpose: Infrastructure and common utilities
- Contains: OS abstraction, I/O streams, JSON persistence, memory management
- Key files: `Persistence.h/cpp` handles config serialization

**examples/:**
- Purpose: Reference implementations and test applications
- Contains: Complete applications showing library usage
- Key examples: lxmf_tdeck (full messaging app), others show specific interfaces

**test/:**
- Purpose: Unit tests for library components
- Contains: Separate test for each major module (crypto, bytes, collections, etc.)
- Key files: `test_main.cpp` in each test directory contains test cases

## Key File Locations

**Entry Points:**
- `src/main.cpp`: Library stub (applications use examples/)
- `examples/lxmf_tdeck/src/main.cpp`: Full LXMF messenger application
- `examples/transport_node_tbeam_supreme/src/main.cpp`: Transport node application

**Configuration:**
- `src/Type.h`: Protocol constants (MTU=500, timeouts, limits)
- `platformio.ini`: Build environments, board configs, library dependencies
- `library.json`: Library metadata for PlatformIO registry

**Core Logic:**
- `src/Transport.h/cpp`: Mesh routing engine (834 + 5214 lines)
- `src/Packet.h/cpp`: Packet structure and operations (381 + 1009 lines)
- `src/Identity.h/cpp`: Cryptographic identity management (1032 lines)
- `src/Link.h/cpp`: Encrypted bidirectional channels (77412 lines total)

**Testing:**
- `test/test_crypto/test_main.cpp`: Cryptography unit tests
- `test/test_bytes/test_main.cpp`: Binary container tests
- `test/test_objects/test_main.cpp`: Wrapper pattern tests

## Naming Conventions

**Files:**
- **Headers**: `.h` extension (e.g., `Transport.h`)
- **Implementation**: `.cpp` extension (e.g., `Transport.cpp`)
- **Type definitions**: File name with capital first letter (e.g., `Packet.h`)
- **Directories**: Capital first letter, PascalCase (e.g., `Cryptography/`, `LXMF/`)

**Functions:**
- **camelCase for public methods**: `send()`, `get_path_table()`, `drop_path()`
- **snake_case for internal/static methods**: `__create_default_config()`, `handle_outgoing()`
- **Callback methods**: Named with subject, e.g., `received_announce()`, `response_timeout_job()`

**Variables:**
- **camelCase for local/parameter variables**: `outgoing_data`, `destination_hash`
- **snake_case with underscore prefix for private members**: `_object`, `_known_destinations_pool`, `_timeout`
- **UPPER_CASE for constants**: `MTU`, `HEADER_MAXSIZE`, `FILEPATH_MAXSIZE`

**Types:**
- **PascalCase for classes**: `Transport`, `Destination`, `Identity`, `BLEPlatform`
- **Suffix `Handler` for callback classes**: `AnnounceHandler`, `RequestHandler`
- **Suffix `Impl` for implementation details**: `InterfaceImpl`, `BLEOperationQueue`
- **Suffix `Data` for internal data structures**: `LinkData`, `ChannelData`, `ResourceData`
- **Prefix `H` for handle/shared_ptr types**: `HInterface`, `HAnnounceHandler`

## Where to Add New Code

**New Feature (e.g., new message type or protocol):**
- **Primary code**: Add to appropriate module directory (e.g., new LXMF extension → `src/LXMF/`)
- **Tests**: Add `test_feature/test_main.cpp` in test/ following existing pattern
- **Example**: Add demonstration in `examples/` if user-facing
- **Integration**: Register with main router/handler classes (e.g., LXMRouter)

**New Component/Module (e.g., new Interface type):**
- **Implementation**: `src/InterfaceType.h/cpp` if top-level, or subdirectory (e.g., `src/BLE/BLEVariant.h`)
- **Base class**: Inherit from `InterfaceImpl` and implement required virtual methods
- **Registration**: Add to interface pool in `Transport.h` or factory in existing platform file
- **Tests**: Create `test_interfacetype/test_main.cpp`

**Utilities/Helpers (e.g., new format encoder):**
- **Shared helpers**: `src/Utilities/HelperName.h/cpp`
- **Crypto primitives**: `src/Cryptography/PrimitiveName.h/cpp`
- **Hardware drivers**: `src/Hardware/DeviceName/ComponentName.h/cpp`
- **UI components**: `src/UI/LXMF/ComponentName.h/cpp`

**Device-specific code (e.g., new board support):**
- **Directory structure**: `src/Hardware/DeviceName/` mirroring T-Deck layout
- **Required files**: `Config.h`, `Display.h`, `Keyboard.h`, etc. matching interface
- **PlatformIO env**: Add to `platformio.ini` with board-specific settings
- **Example**: Create `examples/app_devicename/` showing usage

## Special Directories

**src/Hardware/TDeck/:**
- Purpose: LilyGO T-Deck Plus drivers only
- Generated: No
- Committed: Yes
- Notes: Hardware abstraction; swap with src/Hardware/AnotherDevice for different boards

**src/BLE/platforms/:**
- Purpose: Platform-specific BLE stack implementations
- Generated: No
- Committed: Yes
- Notes: Factory pattern in BLEPlatform.cpp selects implementation at runtime based on compilation flags

**.pio/::**
- Purpose: PlatformIO build artifacts, dependencies, caches
- Generated: Yes
- Committed: No (in .gitignore)
- Notes: Rebuilt during `pio run`; includes downloaded libraries in `.pio/libdeps/`

**examples/lxmf_tdeck/lib/:**
- Purpose: Example-specific library overrides (custom interface implementations)
- Generated: No
- Committed: Yes
- Notes: Contains TCPClientInterface, SX1262Interface, AutoInterface, BLEInterface for testing on desktop

**.planning/codebase/:**
- Purpose: GSD codebase analysis documents
- Generated: By GSD mapper
- Committed: Yes
- Notes: ARCHITECTURE.md, STRUCTURE.md guide future implementation

---

*Structure analysis: 2026-01-23*
