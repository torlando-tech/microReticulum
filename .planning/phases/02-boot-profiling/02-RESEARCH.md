# Phase 2: Boot Profiling - Research

**Researched:** 2026-01-23
**Domain:** ESP32-S3 boot time profiling, timing instrumentation, configuration optimization
**Confidence:** HIGH

## Summary

This phase implements boot profiling instrumentation and configuration optimizations to reduce T-Deck firmware boot time from 15+ seconds to under 5 seconds. The ESP-IDF `esp_timer_get_time()` function provides microsecond-precision timing that begins early in the boot process. The key blocking operations in the current setup() are WiFi connection (30s timeout), GPS time sync (15s timeout), and a 3-second TCP stabilization delay.

Configuration optimizations with the largest impact are: disabling PSRAM memory test (~2 seconds saved for 8MB), reducing log verbosity during boot, and ensuring flash is configured for QIO mode at maximum speed. The project already has QIO flash mode configured correctly.

**Primary recommendation:** Instrument setup() with esp_timer_get_time() around each init function, document per-phase timings, apply configuration optimizations (PSRAM test disable, log level reduction), and restructure blocking waits to be interruptible.

## Standard Stack

The established APIs for this domain:

### Core Timing API
| Function | Purpose | Resolution | When to Use |
|----------|---------|------------|-------------|
| `esp_timer_get_time()` | Microseconds since boot | 1us | All boot profiling |
| `millis()` | Milliseconds since boot | 1ms | Already used in project (compatible) |

### Configuration Flags (sdkconfig/platformio.ini)
| Configuration | Purpose | Impact |
|---------------|---------|--------|
| `CONFIG_SPIRAM_MEMTEST` | PSRAM integrity test at boot | ~1 sec per 4MB |
| `CONFIG_BOOTLOADER_LOG_LEVEL` | Bootloader verbosity | Moderate time savings |
| `CONFIG_LOG_DEFAULT_LEVEL` | Application log level | Moderate time savings |
| `CORE_DEBUG_LEVEL` | Arduino framework log level | PlatformIO-specific |

### Existing Project APIs
| API | Location | Purpose |
|-----|----------|---------|
| `RNS::Utilities::OS::open_file()` | UniversalFileSystem | File persistence for logs |
| `Serial.printf()` | Arduino | Console output |
| `SPIFFS` | Arduino/ESP32 | File storage (already mounted in setup) |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `esp_timer_get_time()` | `millis()` | millis() is 1ms resolution vs 1us, but sufficient for boot profiling |
| SPIFFS for logs | SD card | T-Deck has SPIFFS already configured; SD would require additional setup |
| Serial output | ESP-IDF tracing | Serial is simpler and already working |

**Required Headers:**
```cpp
#include <esp_timer.h>  // for esp_timer_get_time()
// or just use millis() which is already available
```

## Architecture Patterns

### Recommended Module Structure
```
src/
  Instrumentation/
    BootProfiler.h      # Build flag guards, public API
    BootProfiler.cpp    # Per-function timing, file logging
```

### Pattern 1: Boot Profiling Build Flag
**What:** Wrap all boot profiling code in BOOT_PROFILING_ENABLED guard
**When to use:** Always - same pattern as MEMORY_INSTRUMENTATION_ENABLED
**Example:**
```cpp
// BootProfiler.h
#pragma once

#ifdef BOOT_PROFILING_ENABLED

#include <esp_timer.h>

namespace RNS { namespace Instrumentation {

class BootProfiler {
public:
    // Start timing at very beginning of setup()
    static void begin();

    // Mark completion of a phase (logs time since begin and since last mark)
    static void mark(const char* phase_name);

    // Mark end of boot (logs total time, writes to file)
    static void end();

    // Enable file persistence (called after filesystem ready)
    static void enableFilePersistence();

private:
    static int64_t _boot_start;
    static int64_t _last_mark;
    static bool _file_enabled;
};

}} // namespace

// Convenience macros
#define BOOT_PROFILE_BEGIN() RNS::Instrumentation::BootProfiler::begin()
#define BOOT_PROFILE_MARK(name) RNS::Instrumentation::BootProfiler::mark(name)
#define BOOT_PROFILE_END() RNS::Instrumentation::BootProfiler::end()
#define BOOT_PROFILE_ENABLE_FILE() RNS::Instrumentation::BootProfiler::enableFilePersistence()

#else

#define BOOT_PROFILE_BEGIN() ((void)0)
#define BOOT_PROFILE_MARK(name) ((void)0)
#define BOOT_PROFILE_END() ((void)0)
#define BOOT_PROFILE_ENABLE_FILE() ((void)0)

#endif
```

### Pattern 2: Per-Function Timing with Cumulative Total
**What:** Log both individual duration and running total at each checkpoint
**When to use:** Every init function in setup()
**Example:**
```cpp
void BootProfiler::mark(const char* phase_name) {
    int64_t now = esp_timer_get_time();
    int64_t phase_duration = now - _last_mark;
    int64_t total_duration = now - _boot_start;
    _last_mark = now;

    // Format: [BOOT] phase_name: +123ms (total: 456ms)
    snprintf(_log_buffer, sizeof(_log_buffer),
        "[BOOT] %s: +%lldms (total: %lldms)",
        phase_name, phase_duration / 1000, total_duration / 1000);
    Serial.println(_log_buffer);

    if (_file_enabled) {
        appendToLogFile(_log_buffer);
    }
}
```

### Pattern 3: Distinguish Init Time vs Wait Time
**What:** Time blocking waits separately from initialization work
**When to use:** WiFi connection, GPS sync, TCP stabilization
**Example:**
```cpp
// In setup() with profiling:
BOOT_PROFILE_MARK("wifi_init");  // WiFi.mode() + WiFi.begin()

// Timed wait with early exit
int64_t wait_start = esp_timer_get_time();
while (WiFi.status() != WL_CONNECTED && (esp_timer_get_time() - wait_start) < 30000000) {
    delay(100);
}
int64_t wait_time = (esp_timer_get_time() - wait_start) / 1000;
BOOT_PROFILE_MARKF("wifi_wait (%lldms)", wait_time);  // Log actual wait vs timeout
```

### Pattern 4: Log File Rotation
**What:** Keep last N boot profiles, delete oldest
**When to use:** At end of boot profiling, before writing new file
**Example:**
```cpp
// Keep last 5 boot profiles
static const int MAX_BOOT_LOGS = 5;

void rotateBootLogs() {
    // Files: /boot_profile_1.txt through /boot_profile_5.txt
    // Delete oldest, shift numbers down, write to _1
    SPIFFS.remove("/boot_profile_5.txt");
    for (int i = 4; i >= 1; i--) {
        char old_name[32], new_name[32];
        snprintf(old_name, sizeof(old_name), "/boot_profile_%d.txt", i);
        snprintf(new_name, sizeof(new_name), "/boot_profile_%d.txt", i + 1);
        SPIFFS.rename(old_name, new_name);
    }
    // Now write to /boot_profile_1.txt
}
```

### Anti-Patterns to Avoid
- **Logging before Serial ready:** First ~100ms may not show output; begin() should account for this
- **Dynamic allocation in profiler:** Use static buffers only (512 bytes sufficient)
- **File I/O before SPIFFS mounted:** Enable file persistence only after setup_hardware() completes
- **Blocking the profiler:** All profiler operations should be non-blocking

## Don't Hand-Roll

Problems that look simple but have existing solutions:

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Microsecond timing | Custom timer | `esp_timer_get_time()` | Built into ESP-IDF, always available |
| Millisecond timing | Manual math | `millis()` | Arduino standard, already used |
| File persistence | Raw SPIFFS | `UniversalFileSystem::open_file()` | Existing abstraction in project |
| Log formatting | std::string | `snprintf()` into static buffer | No dynamic allocation |

**Key insight:** The project already has all the primitives needed. BootProfiler is primarily organization and build-flag isolation around existing APIs.

## Common Pitfalls

### Pitfall 1: PSRAM Memory Test Adding 2+ Seconds
**What goes wrong:** Default ESP-IDF tests PSRAM integrity at boot, ~1 sec per 4MB
**Why it happens:** `CONFIG_SPIRAM_MEMTEST` enabled by default
**How to avoid:**
- Add to sdkconfig.defaults: `CONFIG_SPIRAM_MEMTEST=n`
- Or add to platformio.ini build_flags: `-DCONFIG_SPIRAM_MEMTEST=0`
**Warning signs:** Boot takes 2+ seconds before any Serial output appears
**Current status:** Not explicitly disabled in sdkconfig.defaults - NEEDS ATTENTION

### Pitfall 2: WiFi Connection 30-Second Timeout
**What goes wrong:** setup_wifi() blocks for up to 30 seconds waiting for WiFi
**Why it happens:** Synchronous polling with long timeout in setup()
**How to avoid:**
- Reduce timeout to 10 seconds
- OR move to background task
- OR use WiFi events (non-blocking)
**Warning signs:** `while (WiFi.status() != WL_CONNECTED && millis() - start < 30000)`
**Current status:** Line 408 in main.cpp has 30-second timeout

### Pitfall 3: GPS Time Sync Blocking
**What goes wrong:** sync_time_from_gps() blocks for 15 seconds on line 1041
**Why it happens:** Waiting for GPS fix before proceeding
**How to avoid:**
- GPS sync is optional (NTP backup exists)
- Reduce timeout or make async
- Continue boot, sync in background
**Warning signs:** `sync_time_from_gps(15000)` in setup()

### Pitfall 4: TCP Stabilization Delay
**What goes wrong:** Hard-coded `delay(3000)` on line 719
**Why it happens:** Waiting for TCP connection to stabilize before announcing
**How to avoid:**
- Reduce to 1 second
- OR use connection callback
- OR make announce retryable
**Warning signs:** `delay(3000)` in setup_lxmf()

### Pitfall 5: Log Verbosity During Boot
**What goes wrong:** Extensive INFO logging slows boot
**Why it happens:** CORE_DEBUG_LEVEL=3 logs everything
**How to avoid:**
- Set CORE_DEBUG_LEVEL=2 (WARNING) for boot
- Dynamically increase log level after boot complete
**Warning signs:** Many INFO lines during boot in Serial output
**Current status:** `CORE_DEBUG_LEVEL=3` in platformio.ini

### Pitfall 6: Display Clear During Init
**What goes wrong:** fill_screen() in Display::init_registers() writes 320x240 pixels via SPI
**Why it happens:** Clearing display to black on init
**How to avoid:**
- Consider if necessary (user sees black anyway)
- Use DMA if available
- Accept as minimal overhead
**Warning signs:** `fill_screen(0x0000)` call
**Current status:** Line 135 in Display.cpp - likely minimal impact but worth measuring

## Code Examples

Verified patterns from official sources and existing codebase:

### Complete Boot Profiler Implementation
```cpp
// Source: ESP-IDF esp_timer documentation + project patterns

#include <esp_timer.h>
#include <SPIFFS.h>

namespace RNS { namespace Instrumentation {

// Static storage
static int64_t _boot_start = 0;
static int64_t _last_mark = 0;
static bool _file_enabled = false;
static char _log_buffer[256];
static char _file_buffer[2048];  // Buffer boot log before file ready
static size_t _file_buffer_used = 0;

void BootProfiler::begin() {
    _boot_start = esp_timer_get_time();
    _last_mark = _boot_start;
    _file_buffer_used = 0;

    // Header (may not show if Serial not ready)
    snprintf(_log_buffer, sizeof(_log_buffer),
        "[BOOT] === Boot Profile Started ===");
    Serial.println(_log_buffer);
    bufferLogLine(_log_buffer);
}

void BootProfiler::mark(const char* phase_name) {
    int64_t now = esp_timer_get_time();
    int64_t phase_ms = (now - _last_mark) / 1000;
    int64_t total_ms = (now - _boot_start) / 1000;
    _last_mark = now;

    snprintf(_log_buffer, sizeof(_log_buffer),
        "[BOOT] %s: +%lldms (total: %lldms)",
        phase_name, phase_ms, total_ms);
    Serial.println(_log_buffer);
    bufferLogLine(_log_buffer);
}

void BootProfiler::end() {
    int64_t total_ms = (esp_timer_get_time() - _boot_start) / 1000;

    snprintf(_log_buffer, sizeof(_log_buffer),
        "[BOOT] === Boot Complete: %lldms ===", total_ms);
    Serial.println(_log_buffer);
    bufferLogLine(_log_buffer);

    // Write to file if enabled
    if (_file_enabled) {
        writeLogFile();
    }

    // Validate against target
    if (total_ms > 5000) {
        Serial.printf("[BOOT] WARNING: Boot exceeded 5 second target by %lldms\n",
            total_ms - 5000);
    }
}

}} // namespace
```

### Configuration Changes for sdkconfig.defaults
```ini
# Add to examples/lxmf_tdeck/sdkconfig.defaults

# Disable PSRAM memory test (saves ~2 seconds for 8MB PSRAM)
# Risk: Undetected PSRAM errors. Mitigated by production testing.
CONFIG_SPIRAM_MEMTEST=n

# Reduce bootloader log level (saves ~100-200ms)
CONFIG_BOOTLOADER_LOG_LEVEL=1

# Note: Application log level controlled by CORE_DEBUG_LEVEL in platformio.ini
```

### Per-Optimization Build Flags Pattern
```ini
# platformio.ini additions for individual optimization toggles

build_flags =
    ; ... existing flags ...

    ; Boot profiling (enable to measure boot time)
    -DBOOT_PROFILING_ENABLED

    ; Individual optimizations (enable one at a time to measure impact)
    -DBOOT_OPT_REDUCED_WIFI_TIMEOUT    ; 10s instead of 30s
    -DBOOT_OPT_SKIP_GPS_SYNC           ; GPS sync moved to background
    -DBOOT_OPT_REDUCED_TCP_DELAY       ; 1s instead of 3s
```

### Integration with main.cpp setup()
```cpp
void setup() {
    Serial.begin(115200);
    delay(100);  // Ensure Serial ready

    BOOT_PROFILE_BEGIN();

    INFO("=== LXMF Messenger for T-Deck Plus ===");
    BOOT_PROFILE_MARK("serial_ready");

    setup_hardware();
    BOOT_PROFILE_MARK("hardware");
    BOOT_PROFILE_ENABLE_FILE();  // SPIFFS now available

    Notification::tone_init();
    BOOT_PROFILE_MARK("tone");

    load_app_settings();
    BOOT_PROFILE_MARK("settings");

    setup_gps();
    BOOT_PROFILE_MARK("gps_init");

    // Timed separately - this is a blocking wait
    if (app_settings.gps_time_sync) {
        int64_t wait_start = esp_timer_get_time();
        sync_time_from_gps(5000);  // Reduced from 15s
        BOOT_PROFILE_MARKF("gps_sync_wait (%lldms)",
            (esp_timer_get_time() - wait_start) / 1000);
    }

    setup_wifi();
    BOOT_PROFILE_MARK("wifi_init+wait");

    setup_lvgl_and_ui();
    BOOT_PROFILE_MARK("lvgl");

    setup_reticulum();
    BOOT_PROFILE_MARK("reticulum");

    setup_lxmf();
    BOOT_PROFILE_MARK("lxmf");

    setup_ui_manager();
    BOOT_PROFILE_MARK("ui_manager");

    BOOT_PROFILE_END();

    INFO("=== System Ready ===");
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `millis()` timing | `esp_timer_get_time()` | ESP-IDF 4.0+ | Microsecond precision |
| Fixed delay waits | Event-driven/callback | Modern patterns | Non-blocking boot |
| Full logging at boot | Reduced log level | Performance tuning | Faster boot |
| PSRAM test enabled | Optional/configurable | ESP-IDF 5.x | ~2 sec savings |

**Deprecated/outdated:**
- `ets_get_cpu_frequency()` for timing: Use `esp_timer_get_time()` instead
- Manual timer configuration: esp_timer handles hardware abstraction

## Open Questions

Things that couldn't be fully resolved:

1. **Exact boot time breakdown before instrumentation**
   - What we know: Total boot is 15+ seconds
   - What's unclear: Which phases dominate (WiFi wait likely, but unconfirmed)
   - Recommendation: Implement profiling first, measure, then optimize based on data

2. **T-Deck SPIFFS partition size**
   - What we know: SPIFFS is mounted and working
   - What's unclear: How much space available for boot logs
   - Recommendation: Check SPIFFS.totalBytes() at runtime; 5x 2KB logs = 10KB is minimal

3. **Minimum acceptable WiFi timeout**
   - What we know: Current is 30 seconds
   - What's unclear: How fast WiFi typically connects on this network
   - Recommendation: Profile actual connect times, set timeout to 2x typical + margin

4. **GPS sync necessity**
   - What we know: NTP is fallback if GPS fails
   - What's unclear: Use cases requiring GPS time sync at boot
   - Recommendation: Make GPS sync optional/background by default

## Sources

### Primary (HIGH confidence)
- [ESP-IDF esp_timer v5.5 Documentation](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-reference/system/esp_timer.html) - `esp_timer_get_time()` API
- [ESP-IDF Speed Optimization Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/performance/speed.html) - Boot time reduction strategies
- [ESP-IDF Application Startup Flow](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/startup.html) - Boot sequence phases
- [ESP-IDF Bootloader Guide](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-guides/bootloader.html) - Bootloader log level configuration

### Secondary (MEDIUM confidence)
- [ESP32 Forum: Boot time optimization](https://esp32.com/viewtopic.php?t=9448) - Community optimization tips
- [PlatformIO ESP32-S3 Flash/PSRAM Config](https://github.com/sivar2311/ESP32-PlatformIO-Flash-and-PSRAM-configurations) - Configuration reference

### Codebase (HIGH confidence - existing patterns)
- `/home/tyler/repos/public/microReticulum/src/Instrumentation/MemoryMonitor.h` - Build flag pattern, FreeRTOS timer pattern
- `/home/tyler/repos/public/microReticulum/examples/lxmf_tdeck/src/main.cpp` - Current setup() structure, blocking operations
- `/home/tyler/repos/public/microReticulum/examples/lxmf_tdeck/sdkconfig.defaults` - Current ESP-IDF configuration
- `/home/tyler/repos/public/microReticulum/examples/lxmf_tdeck/platformio.ini` - Build flags, QIO flash mode
- `/home/tyler/repos/public/microReticulum/.planning/research/STACK.md` - Prior boot optimization research

## Metadata

**Confidence breakdown:**
- Timing API: HIGH - Official ESP-IDF documentation, verified in existing codebase patterns
- Configuration optimizations: HIGH - Verified with Espressif documentation
- Blocking operations: HIGH - Direct analysis of main.cpp
- File rotation pattern: MEDIUM - Standard practice, not verified against SPIFFS specifics

**Research date:** 2026-01-23
**Valid until:** 2026-02-23 (ESP-IDF APIs are stable)
