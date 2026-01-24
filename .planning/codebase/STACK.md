# Technology Stack

**Analysis Date:** 2026-01-23

## Languages

**Primary:**
- C++ 11 - Core library and application code
- C - Cryptographic primitives and utility code

**Secondary:**
- Python - Project bootstrap documentation (reference implementation context)

## Runtime

**Environment:**
- Bare-metal embedded systems via PlatformIO / Arduino framework
- FreeRTOS for task scheduling and concurrency (ESP32, nRF52)
- Arduino IDE / VSCode + PlatformIO for development

**Package Manager:**
- PlatformIO (C/C++ embedded package manager)
- Platform specifications defined in `platformio.ini`

## Frameworks

**Core Communication:**
- Reticulum Network Stack - Custom C++ port of Python Reticulum reference implementation (`src/`)
- Custom microReticulum abstraction layer supporting implicit object sharing semantics

**BLE (Bluetooth Low Energy):**
- NimBLE-Arduino (`src/BLE/platforms/NimBLEPlatform.cpp/h`) - Primary BLE implementation for ESP32
- Platform abstraction interface at `src/BLE/BLEPlatform.h` enabling multiple BLE implementations
- Dual-mode operation (central + peripheral simultaneously)
- Custom BLE fragmentation/reassembly: `src/BLE/BLEFragmenter.cpp`, `src/BLE/BLEReassembler.cpp`
- MAC rotation support for privacy (Protocol v2.2)

**Display/UI:**
- LVGL (Light and Versatile Graphics Library) - UI framework via `src/UI/LVGL/LVGLInit.cpp/h`
- FreeRTOS task support for dedicated UI rendering
- Display abstraction: `src/Display.cpp/h` for OLED displays (T-Beam Supreme SH1106)
- Adafruit SH110X driver (`adafruit/Adafruit SH110X@^2.1.10`)
- Adafruit GFX Library (`adafruit/Adafruit GFX Library@^1.11.9`)

**Testing:**
- Unity test framework (PlatformIO native)
- Native platform builds for host-based unit testing (`env:native`, `env:native17`, `env:native20`)

**Build/Development:**
- CMake - Cross-platform build support (`CMakeLists.txt`)
- PlatformIO - Hardware-specific compilation and deployment
- Custom build scripts: `link_bz2.py` for bzip2 integration

## Key Dependencies

**Critical:**
- ArduinoJson 7.4.2+ - JSON serialization/parsing for configuration and protocols
- MsgPack 0.4.2+ - Binary message serialization for efficient packet encoding
- Crypto (custom fork: `https://github.com/attermann/Crypto.git`) - Cryptographic operations
  - Ed25519 signatures
  - X25519 key exchange
  - AES encryption (AES, AES-256, AES-CBC)
  - HKDF key derivation
  - HMAC authentication
  - PKCS7 padding
  - Fernet authenticated encryption
  - BZ2 compression

**Platform-Specific:**
- NimBLE-Arduino (implicit, pulled by ESP32 builds with `USE_NIMBLE=1`)
- BluedroidPlatform (alternative, not yet active - `src/BLE/platforms/BluedroidPlatform.h`)

**Display (conditional):**
- Adafruit SH110X 2.1.10+ (T-Beam Supreme OLED driver)
- Adafruit GFX Library 1.11.9+ (graphics primitives)
- LVGL (implicitly included via ARDUINO framework on display-enabled boards)

## Configuration

**Environment:**
- Device name configuration via PlatformIO build environment
- Storage path: `RNS_PERSIST_PATHS` compile flag enables persistence
- File system support via `RNS_USE_FS` compile flag
- Memory allocator: `RNS_USE_ALLOCATOR=1`, `RNS_USE_TLSF=1` for nRF52 boards

**Build:**
- `platformio.ini` - Main configuration file with environment definitions
- `library.json` - Arduino Library metadata
- `library.properties` - Arduino IDE compatibility properties
- `CMakeLists.txt` - CMake build configuration for non-embedded builds

**Compiler Flags:**
- `-std=c++11` (base), `-std=c++17`, `-std=c++20` variants available
- `-Wall` with selective warnings disabled (`-Wno-missing-field-initializers`, `-Wno-format`, `-Wno-unused-parameter`)
- Define flags: `LIBRARY_TEST`, `NATIVE`, `BOARD_ESP32`, `BOARD_TBEAM_SUPREME`, `HAS_DISPLAY`, `DISPLAY_TYPE_SH1106`, `USE_NIMBLE`

## Platform Requirements

**Development:**
- VSCode with PlatformIO IDE extension
- PlatformIO CLI (pio)
- CMake 3.14+ (for non-embedded builds)
- C++11-compatible compiler

**Target Hardware:**
- ESP32 (primary: T-Beam, T-Beam Supreme with ESP32-S3)
- nRF52840 (Nordic Semiconductor WisCore RAK4631)
- Generic ARM Cortex-M for Arduino-compatible boards
- Native x86/x64 Linux for testing (`env:native*`)

**Supported Platforms in platformio.ini:**
- `native` - Desktop/host compilation for testing
- `espressif32` - ESP32 variants (includes NimBLE)
- `espressif32@6.4.0` - ESP32 with specific arduino-esp32 2.x version (native ble_gap_connect support)
- `nordicnrf52` - Nordic nRF52 series

**Memory & Hardware:**
- ESP32-S3 (T-Beam Supreme): 8MB flash, PSRAM support with cache fix (`-mfix-esp32-psram-cache-issue`)
- Flash partitioning: `no_ota.csv` (no over-the-air updates in current config)
- UART/Serial: 115200 baud default monitor speed

---

*Stack analysis: 2026-01-23*
