# Technology Stack

**Analysis Date:** 2026-01-24

## Languages

**Primary:**
- C++ (C++11) - Main application code, hardware drivers, messaging logic, and LXMF implementation
- C - Firmware components, SDK code, device drivers via ESP-IDF

**Secondary:**
- Python - Not detected in this project

## Runtime

**Environment:**
- ESP-IDF (Espressif IoT Development Framework) - Based on FreeRTOS, provides threading and system capabilities
- Arduino Framework (ESP32) - Abstracts hardware interfaces, GPIO, serial communication
- FreeRTOS - Real-time operating system kernel used by ESP-IDF

**Package Manager:**
- PlatformIO - Manages build, dependencies, and device firmware uploads
- Arduino Library Manager - Manages Arduino framework libraries (integrated via PlatformIO)

## Frameworks

**Core:**
- Reticulum - Mesh networking framework (microReticulum variant for embedded systems) - Used in `src/Reticulum.h`, provides networking abstraction
- LXMF (Lightweight eXtendable Messaging Format) - Message routing and delivery layer built on Reticulum - Provided by `src/LXMF/`

**UI:**
- LVGL (Light and Versatile Graphics Library) v8.3.11 - Graphics framework for T-Deck Plus display
- T-Deck UI Library (`lib/tdeck_ui`) - Hardware-specific UI components and display abstraction
- LXMF UI Manager (`UI/LXMF/UIManager.h`) - Application UI logic for messaging interface

**Radio/Networking:**
- RadioLib v6.0 - Radio modem abstraction for SX1262 LoRa module
- NimBLE-Arduino v2.1.0 - Bluetooth Low Energy stack (default) - Uses ~100KB less RAM than Bluedroid
- Bluedroid BLE - Fallback Bluetooth stack via `[env:tdeck-bluedroid]` (uses more RAM)

**Hardware Drivers:**
- TinyGPSPlus v1.0.3 - GPS module parsing and location data
- Crypto v0.4.0 - Ed25519 signature and encryption operations (16KB stack required)
- MsgPack v0.4.2 - Message serialization/deserialization
- ArduinoJson v7.4.2 - JSON parsing for configuration and message metadata

## Key Dependencies

**Critical:**
- LVGL v8.3.11 - Display and UI rendering for T-Deck Plus 1.47" LCD
- NimBLE-Arduino v2.1.0 - Bluetooth mesh networking support (dual-mode: central + peripheral)
- RadioLib v6.0 - LoRa radio control via SX1262 modem
- MsgPack v0.4.2 - Wire format for LXMF messages and peer discovery
- ArduinoJson v7.4.2 - Application settings and message storage (filesystem JSON files)

**Infrastructure:**
- Crypto v0.4.0 - Ed25519 digital signatures for identity and message authenticity
- TinyGPSPlus v1.0.3 - GPS time synchronization and location-based timezone calculation
- Custom libraries:
  - `universal_filesystem` - SPIFFS/LittleFS abstraction for message storage
  - `sx1262_interface` - RNode-compatible LoRa interface via SX1262
  - `ble_interface` - BLE-Reticulum protocol v2.2 mesh networking
  - `auto_interface` - IPv6 peer discovery via WiFi
  - `tone` - I2S speaker audio notifications

## Configuration

**Environment:**
- ESP32-S3 DevKit board configuration with 8MB Flash + 8MB PSRAM
- Two environments defined:
  - `[env:tdeck]` - Primary (default) using NimBLE stack
  - `[env:tdeck-bluedroid]` - Fallback using Bluedroid (higher RAM usage)

**Build:**
- Build type: release (optimized)
- Memory optimization flags:
  - `-DMEMORY_INSTRUMENTATION_ENABLED` - Heap/stack monitoring (optional)
  - `-DBOOT_PROFILING_ENABLED` - Boot timing instrumentation (optional)
  - `-DBOOT_REDUCED_LOGGING` - Reduces INFO-level output during boot
  - `-DARDUINO_LOOP_STACK_SIZE=16384` - Increased to 16KB for Ed25519 crypto operations
- Include paths configured for microReticulum libraries and hardware abstraction

**SDK Configuration (`sdkconfig.defaults`):**
- `CONFIG_BT_NIMBLE_ENABLED=y` - BLE stack selection
- `CONFIG_SPIRAM=y` with `CONFIG_SPIRAM_USE_MALLOC=y` - External RAM allocation for buffers
- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=64` - Move 16-200 byte allocations to PSRAM (reduces heap fragmentation)
- `CONFIG_COMPILER_CXX_EXCEPTIONS=y` - C++ exception support required
- `CONFIG_FREERTOS_HZ=1000` - Task scheduling frequency
- `CONFIG_ESP_TASK_WDT_EN=y` - Task watchdog enabled (10s timeout)

## Platform Requirements

**Development:**
- PlatformIO CLI for building and uploading
- USB connection to T-Deck Plus for firmware upload (CDC-ACM)
- 8MB flash available on device

**Production:**
- T-Deck Plus (LilyGO) - ESP32-S3 with:
  - 8MB Flash (QIO mode)
  - 8MB PSRAM (OPI mode) at 80MHz
  - 1.47" IPS LCD display (170x320)
  - SX1262 LoRa modem (900 MHz ISM band)
  - L76K/L76B GPS module (UART1)
  - Keyboard + trackball input
  - Touch panel
  - SD card slot for logging
  - Speaker with I2S interface
- Networking: LoRa (SX1262), BLE mesh (NimBLE), WiFi (via Auto Interface)

---

*Stack analysis: 2026-01-24*
