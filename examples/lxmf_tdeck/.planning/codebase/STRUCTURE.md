# Codebase Structure

**Analysis Date:** 2026-01-24

## Directory Layout

```
examples/lxmf_tdeck/
├── src/                           # Application-specific code
│   ├── main.cpp                   # Bootstrap and event loop
│   ├── TCPClientInterface.cpp      # WiFi/TCP transport implementation
│   ├── TCPClientInterface.h        # TCP interface header
│   └── HDLC.h                      # TCP frame encoding (0x7E flag format)
├── lib/                           # Local libraries (not in parent repo)
│   ├── sx1262_interface/          # LoRa radio wrapper
│   │   ├── SX1262Interface.h
│   │   └── SX1262Interface.cpp
│   ├── universal_filesystem/      # Filesystem abstraction (SPIFFS/LittleFS)
│   │   ├── UniversalFileSystem.h
│   │   └── UniversalFileSystem.cpp
│   ├── tone/                      # Audio notification driver
│   │   ├── Tone.h
│   │   └── Tone.cpp
│   ├── lv_conf.h                  # LVGL configuration overrides
│   ├── lv_mem_hybrid.h            # Memory allocation config
│   └── tdeck_ui/                  # (external symlink or git submodule)
├── platformio.ini                 # Build configuration (ESP32-S3 targets)
├── sdkconfig.defaults             # ESP-IDF SDK configuration
├── partitions.csv                 # Flash partitions (8MB split)
└── .pio/                          # Build artifacts (ignored)
```

**Parent Repository Structure:**
```
microReticulum/
├── src/                           # Core Reticulum + LXMF + UI + BLE
│   ├── Reticulum.h/cpp            # Network core
│   ├── Transport.h/cpp            # Packet routing
│   ├── Interface.h/cpp            # Interface abstraction
│   ├── Destination.h/cpp          # Endpoint identities
│   ├── Identity.h/cpp             # Cryptographic identity
│   ├── Packet.h/cpp               # Packet framing
│   ├── Link.h/cpp                 # Point-to-point connections
│   ├── Channel.h/cpp              # Ordered message channels
│   ├── LXMF/                      # High-level messaging
│   │   ├── LXMRouter.h/cpp        # Message routing
│   │   ├── LXMessage.h/cpp        # Message envelope
│   │   ├── MessageStore.h/cpp     # Filesystem persistence
│   │   ├── PropagationNodeManager.h/cpp
│   │   ├── LXStamper.h/cpp        # Signature generation
│   │   └── Type.h
│   ├── UI/                        # User interface layer
│   │   ├── LVGL/                  # LVGL initialization
│   │   │   ├── LVGLInit.h
│   │   │   └── LVGLLock.h         # Mutex for thread-safe LVGL calls
│   │   └── LXMF/                  # LXMF-specific UI (screens, manager)
│   │       ├── UIManager.h        # Central UI orchestrator
│   │       ├── SettingsScreen.h   # Settings UI component
│   │       └── ...                # Various screen implementations
│   ├── BLE/                       # Bluetooth mesh interface
│   │   ├── BLEInterface.h
│   │   ├── BLEPeerManager.h
│   │   ├── BLEIdentityManager.h
│   │   ├── BLETypes.h
│   │   └── platforms/             # Platform-specific BLE implementations
│   ├── Hardware/TDeck/            # T-Deck Plus hardware drivers
│   │   ├── Config.h               # Pin definitions
│   │   ├── Display.h/cpp          # SPI display driver
│   │   ├── Keyboard.h/cpp         # I2C keyboard matrix
│   │   ├── Touch.h/cpp            # Touch screen (capacitive)
│   │   ├── Trackball.h/cpp        # Optical trackball
│   │   └── SDLogger.h/cpp         # SD card crash logging
│   ├── Instrumentation/           # Performance monitoring
│   │   ├── MemoryMonitor.h/cpp    # Heap/stack monitoring
│   │   └── BootProfiler.h/cpp     # Boot timing analysis
│   ├── Cryptography/              # Crypto primitives
│   │   ├── Random.h
│   │   └── ...
│   ├── Utilities/                 # Helper utilities
│   │   ├── OS.h                   # Time, filesystem registration
│   │   └── ...
│   ├── Log.h/cpp                  # Logging framework
│   ├── Bytes.h/cpp                # Binary data container
│   ├── BytesPool.h                # Memory pool for Bytes objects
│   ├── Type.h                     # Type definitions
│   └── ...
├── lib/
│   └── libbz2/                    # Compression library (vendored)
├── examples/
│   ├── lxmf_tdeck/                # THIS PROJECT (T-Deck Plus messenger)
│   └── ...                         # Other example projects
└── test/                          # Unit tests
```

## Directory Purposes

**examples/lxmf_tdeck/src/:**
- Purpose: Application logic and transport implementations specific to this T-Deck example
- Contains: Main entry point, TCP client interface, HDLC frame encoding
- Key files: `main.cpp` (1438 lines, bootstrap + event loop), `TCPClientInterface.h/cpp`

**examples/lxmf_tdeck/lib/sx1262_interface/:**
- Purpose: LoRa radio driver abstraction for SX1262 chip (RadioLib wrapper)
- Contains: RadioLib configuration, packet transmission/reception, RSSI/SNR measurement
- Key files: `SX1262Interface.h` (configuration struct and interface wrapper)

**examples/lxmf_tdeck/lib/universal_filesystem/:**
- Purpose: Cross-platform filesystem abstraction (SPIFFS on ESP32, LittleFS on NRF52, POSIX on desktop)
- Contains: File I/O wrappers with polymorphic FileStream implementation
- Key files: `UniversalFileSystem.h` (provides `RNS::FileSystemImpl` interface)

**examples/lxmf_tdeck/lib/tone/:**
- Purpose: Audio notification playback (beep for incoming messages)
- Contains: PWM-based tone generation via LEDC peripheral
- Key files: `Tone.h` (simple frequency/duration interface)

**src/LXMF/:**
- Purpose: Message-oriented routing and storage
- Contains: Router with delivery tracking, persistent message store, propagation node discovery
- Key files: `LXMRouter.h/cpp` (core messaging), `MessageStore.h/cpp` (filesystem persistence)

**src/UI/LXMF/:**
- Purpose: Screens and state management for LXMF messaging application
- Contains: Message list, conversation, settings screens; UIManager orchestration
- Key files: `UIManager.h` (central coordinator), `SettingsScreen.h` (settings editing)

**src/Hardware/TDeck/:**
- Purpose: Hardware abstraction for T-Deck Plus peripherals
- Contains: Pin definitions, SPI/I2C drivers for display/keyboard/touch/trackball
- Key files: `Config.h` (pin mappings), `Display.h/cpp` (LCD driver)

**src/BLE/:**
- Purpose: Bluetooth mesh networking (peer discovery, fragmentation/reassembly, identity management)
- Contains: BLE peer manager, fragmentation protocol, identity key exchange
- Key files: `BLEInterface.h` (network interface), `BLEPeerManager.h` (connection management)

## Key File Locations

**Entry Points:**
- `examples/lxmf_tdeck/src/main.cpp` - Arduino setup() and loop() entry points
- Line 1031: `void setup()` - initialization phase
- Line 1160: `void loop()` - event loop called repeatedly

**Configuration:**
- `examples/lxmf_tdeck/platformio.ini` - Build settings, dependency list, compiler flags
- `examples/lxmf_tdeck/sdkconfig.defaults` - ESP-IDF peripheral configuration
- `examples/lxmf_tdeck/partitions.csv` - Flash memory partitions (8MB total)
- `src/Hardware/TDeck/Config.h` - Hardware pin definitions

**Core Logic:**
- `src/LXMF/LXMRouter.h/cpp` - Message routing engine
- `src/LXMF/MessageStore.h/cpp` - Filesystem message persistence
- `src/Transport.h/cpp` - Network packet routing
- `src/Reticulum.h/cpp` - Network bootstrap and lifecycle

**Networking:**
- `examples/lxmf_tdeck/src/TCPClientInterface.h/cpp` - TCP/WiFi transport
- `examples/lxmf_tdeck/lib/sx1262_interface/SX1262Interface.h/cpp` - LoRa radio
- `src/BLE/BLEInterface.h` - Bluetooth mesh
- `src/Interface.h/cpp` - Interface abstraction base class

**UI & Display:**
- `src/UI/LVGL/LVGLInit.h` - LVGL rendering task and thread safety
- `src/UI/LXMF/UIManager.h` - Message/contact/screen state management
- `src/Hardware/TDeck/Display.h/cpp` - SPI LCD driver
- `src/Hardware/TDeck/Keyboard.h/cpp` - I2C keyboard matrix scanner

**Testing:**
- `test/` - Unit test projects using same codebase
- Test builds use desktop environment (POSIX filesystem, no hardware)

## Naming Conventions

**Files:**
- `.h` - Header file (declaration)
- `.cpp` - Implementation file
- `Config.h` - Configuration/constants (no implementation)
- Lowercase with underscores: `tcp_interface.h`, `message_store.cpp`
- PascalCase class names: `TCPClientInterface`, `SX1262Interface`

**Directories:**
- PascalCase for module names: `LXMF/`, `Hardware/`, `UI/`
- Lowercase for hardware variants: `TDeck/` (contains variant-specific drivers)
- Platform-specific suffixes: `BLE/platforms/` for NimBLE vs Bluedroid

**Functions/Variables:**
- `snake_case` for public functions: `set_target_host()`, `start()`, `process_inbound()`
- `_snake_case` for private members: `_frame_buffer`, `_target_host`
- `SCREAMING_SNAKE_CASE` for constants: `DEFAULT_TCP_PORT`, `HW_MTU`

**Types:**
- `PascalCase` for classes/structs: `LXMessage`, `SX1262Config`
- Exception: C-style enums use `SCREAMING_SNAKE_CASE` members: `Type::Message::NEW`, `Type::Message::DELIVERED`

## Where to Add New Code

**New Feature (e.g., new message type or notification):**
- Primary code: `examples/lxmf_tdeck/src/main.cpp` (if adding to event loop) or `src/LXMF/` (if adding message handling)
- Tests: `test/test_example/` or create new test project
- UI: `src/UI/LXMF/` for new screens

**New Transport Interface (e.g., new radio type):**
- Implementation: `examples/lxmf_tdeck/lib/{new_interface_name}/` (following SX1262Interface pattern)
- Header: Inherit from `RNS::InterfaceImpl`, implement `start()`, `stop()`, `loop()`, `send_outgoing()`
- Integration: Register in `main.cpp` setup_reticulum() with `Transport::register_interface()`

**New Hardware Driver (e.g., new sensor on T-Deck):**
- Location: `src/Hardware/TDeck/` if variant-agnostic, or `src/Hardware/TDeck/{NewDriver}.h/cpp`
- Pattern: Static class with pin definitions, initialization, polling methods
- Initialize in `setup_hardware()` (line 470-496 in main.cpp)

**Utilities/Helpers:**
- Shared code used by multiple modules: `src/Utilities/`
- Single-use helpers: Keep in same file as consumer
- Algorithm/math: `src/Utilities/` if reusable across projects

**Settings/Configuration:**
- New app setting: Add field to `UI::LXMF::AppSettings` struct
- NVS persistence: Add `Preferences::getX()` call in `load_app_settings()` (line 346-400)
- UI control: Add to settings screen implementation in `src/UI/LXMF/SettingsScreen.h`
- Runtime application: Add to settings save callback (line 836-1022)

## Special Directories

**examples/lxmf_tdeck/.pio/:**
- Purpose: Build artifacts and dependencies
- Generated: Yes (by PlatformIO during `pio build`)
- Committed: No (.gitignore)
- Contents: Compiled object files, downloaded libraries, build metadata

**examples/lxmf_tdeck/.planning/:**
- Purpose: GSD planning and documentation
- Generated: No (manually created)
- Committed: Yes (for team reference)
- Contents: Phase plans, research notes, codebase analysis docs

**src/.planning/:**
- Purpose: Core library design and research docs
- Generated: No
- Committed: Yes
- Contents: Architecture notes, decision records

**test/:**
- Purpose: Unit and integration testing
- Generated: Partially (build artifacts)
- Committed: Yes (test code)
- Contents: Multiple test projects, each with own platformio.ini and src/

---

*Structure analysis: 2026-01-24*
