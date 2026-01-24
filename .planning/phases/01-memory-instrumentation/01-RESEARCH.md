# Phase 1: Memory Instrumentation - Research

**Researched:** 2026-01-23
**Domain:** ESP32-S3 FreeRTOS heap/stack monitoring
**Confidence:** HIGH

## Summary

This phase implements runtime memory monitoring for ESP32-S3 with PSRAM, capturing heap fragmentation and FreeRTOS task stack metrics. The ESP-IDF heap_caps APIs are the authoritative solution for heap monitoring, providing direct access to free heap, largest contiguous block, and per-region (internal/PSRAM) statistics. Task stack monitoring uses `uxTaskGetStackHighWaterMark()` which is available in the Arduino framework without additional configuration.

The main challenge is task enumeration: `uxTaskGetSystemState()` requires `configUSE_TRACE_FACILITY=1` which is disabled in the precompiled Arduino framework. The practical workaround is to track task handles explicitly at creation time rather than enumerating dynamically.

**Primary recommendation:** Use ESP-IDF heap_caps APIs for heap monitoring, maintain an explicit registry of task handles for stack monitoring, and implement a FreeRTOS software timer for periodic logging.

## Standard Stack

The established APIs for this domain:

### Core ESP-IDF Heap APIs
| Function | Purpose | When to Use |
|----------|---------|-------------|
| `heap_caps_get_free_size(caps)` | Total free bytes for memory region | Periodic monitoring |
| `heap_caps_get_largest_free_block(caps)` | Largest contiguous allocation possible | **Fragmentation indicator** |
| `heap_caps_get_minimum_free_size(caps)` | Low watermark since boot | Identify worst-case usage |

### FreeRTOS Task APIs
| Function | Purpose | Availability |
|----------|---------|--------------|
| `uxTaskGetStackHighWaterMark(handle)` | Minimum free stack since task start | Always available |
| `xTaskGetHandle(name)` | Get handle from task name | Available (uses string search) |
| `pcTaskGetName(handle)` | Get task name from handle | Always available |

### Memory Capability Flags
| Flag | Purpose |
|------|---------|
| `MALLOC_CAP_INTERNAL` | Internal SRAM (~320KB usable on ESP32-S3) |
| `MALLOC_CAP_SPIRAM` | PSRAM (8MB on T-Deck) |
| `MALLOC_CAP_DEFAULT` | Default heap (internal first, then PSRAM) |
| `MALLOC_CAP_8BIT` | Byte-addressable memory (commonly used) |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `heap_caps_*` | `ESP.getFreeHeap()` | Arduino wrappers are simpler but less detailed |
| `uxTaskGetSystemState()` | Explicit task registry | System state requires recompiling framework |
| FreeRTOS software timer | `esp_timer_create()` | esp_timer runs from ISR context, software timer from daemon task |

**Required Header:**
```cpp
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
```

## Architecture Patterns

### Recommended Module Structure
```
src/
  Instrumentation/
    MemoryMonitor.h      # Public API and build flag guards
    MemoryMonitor.cpp    # Implementation (ESP32-specific)
```

### Pattern 1: Build Flag Isolation
**What:** Wrap all instrumentation code in preprocessor guards
**When to use:** Always - instrumentation must be zero-overhead when disabled
**Example:**
```cpp
// MemoryMonitor.h
#pragma once

#ifdef MEMORY_INSTRUMENTATION_ENABLED

namespace RNS { namespace Instrumentation {

class MemoryMonitor {
public:
    static bool init(uint32_t interval_ms = 30000);
    static void stop();
    static void registerTask(TaskHandle_t handle, const char* name);
    static void unregisterTask(TaskHandle_t handle);
    // Optional: runtime verbosity toggle (Claude's discretion)
    static void setVerbose(bool verbose);
private:
    static void timerCallback(TimerHandle_t timer);
};

}} // namespace

#else
// Stub macros when disabled
#define MEMORY_MONITOR_INIT(interval) ((void)0)
#define MEMORY_MONITOR_REGISTER_TASK(handle, name) ((void)0)
#define MEMORY_MONITOR_UNREGISTER_TASK(handle) ((void)0)
#endif
```

### Pattern 2: Task Handle Registry
**What:** Maintain explicit list of task handles for stack monitoring
**When to use:** When `uxTaskGetSystemState()` is unavailable
**Example:**
```cpp
// Source: ESP-IDF FreeRTOS documentation
struct TaskEntry {
    TaskHandle_t handle;
    char name[configMAX_TASK_NAME_LEN];
};

static TaskEntry _task_registry[MAX_MONITORED_TASKS];
static size_t _task_count = 0;

void registerTask(TaskHandle_t handle, const char* name) {
    if (_task_count < MAX_MONITORED_TASKS) {
        _task_registry[_task_count].handle = handle;
        strncpy(_task_registry[_task_count].name, name, configMAX_TASK_NAME_LEN - 1);
        _task_count++;
    }
}
```

### Pattern 3: Fragmentation Calculation
**What:** Calculate fragmentation percentage from heap metrics
**When to use:** Every monitoring interval
**Example:**
```cpp
// Source: ESP-IDF Heap Memory Debugging documentation
size_t free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
uint8_t fragmentation = (free > 0) ? (100 - (largest * 100 / free)) : 0;
// fragmentation > 50% is problematic
```

### Pattern 4: Dual Output (Serial + File)
**What:** Log to both serial and SD card file simultaneously
**When to use:** As specified in user decisions
**Example:**
```cpp
void logMemoryStats(const char* msg) {
    // Always log to serial
    Serial.println(msg);

    // Log to file if available
    if (_log_file) {
        _log_file.write((const uint8_t*)msg, strlen(msg));
        _log_file.write('\n');
        _log_file.flush();  // Ensure data is written
    }
}
```

### Anti-Patterns to Avoid
- **Dynamic allocation in monitor:** Use static buffers only - instrumentation must not cause fragmentation
- **Logging from ISR context:** Use FreeRTOS software timer, not esp_timer
- **Blocking file I/O in timer callback:** Use non-blocking or queue-based approach
- **String formatting with std::string:** Use snprintf into static char buffer

## Don't Hand-Roll

Problems that look simple but have existing solutions:

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Heap statistics | Manual malloc tracking | `heap_caps_*` APIs | Built into ESP-IDF, accounts for all allocations |
| Fragmentation metric | Custom block walker | `heap_caps_get_largest_free_block()` | Authoritative, O(n) internal implementation |
| Periodic execution | `millis()` polling | `xTimerCreate()` | Proper FreeRTOS integration, doesn't block tasks |
| Task enumeration | Walking internal lists | Explicit registry or `xTaskGetHandle()` | Internal FreeRTOS structures are private |
| Time formatting | Custom sprintf | `strftime()` with RTC or `millis()` | Standard, tested |

**Key insight:** The ESP-IDF heap implementation already tracks all the metrics needed. The heap_caps APIs expose this internal state efficiently without additional overhead.

## Common Pitfalls

### Pitfall 1: Arduino Framework Task Enumeration Limitation
**What goes wrong:** `uxTaskGetSystemState()` causes undefined reference or link errors
**Why it happens:** Arduino-esp32 framework is precompiled with `configUSE_TRACE_FACILITY=0`
**How to avoid:**
- Track task handles explicitly at creation time
- Use `xTaskGetHandle()` to look up known task names
- Do NOT attempt to enumerate unknown tasks
**Warning signs:** "undefined reference to uxTaskGetSystemState" at link time

### Pitfall 2: File I/O in Timer Callback
**What goes wrong:** Watchdog timeout or system instability
**Why it happens:** SD card writes can block for 10-100ms
**How to avoid:**
- Buffer log entries in RAM
- Use a separate low-priority task for file writes
- Or accept sync writes if interval is long (30s is fine)
**Warning signs:** Task watchdog triggers, "Task watchdog got triggered"

### Pitfall 3: Stack Overflow During Logging
**What goes wrong:** Stack overflow while measuring stack
**Why it happens:** `snprintf` and `Serial.printf` can use significant stack
**How to avoid:**
- Use static buffers (not stack-allocated)
- Keep format strings simple
- Test with `uxTaskGetStackHighWaterMark()` on the logging task itself
**Warning signs:** Guru Meditation Error: Core X panic'ed (Stack canary watchpoint triggered)

### Pitfall 4: PSRAM vs Internal RAM Confusion
**What goes wrong:** Misleading fragmentation numbers
**Why it happens:** Reporting combined metrics masks internal RAM exhaustion
**How to avoid:**
- Always report internal and PSRAM separately
- Internal RAM fragmentation is the critical metric
- PSRAM fragmentation is less concerning due to size
**Warning signs:** Crashes despite high total free heap reported

### Pitfall 5: Session Filename Generation Without RTC
**What goes wrong:** Overwriting previous log files
**Why it happens:** No RTC means no real timestamp for filenames
**How to avoid:**
- Use boot counter from NVS + millis-based suffix
- Or use incrementing number (memory_001.txt, memory_002.txt)
- Check for existing files and increment
**Warning signs:** Log files always named the same, data lost on reboot

## Code Examples

Verified patterns from official sources:

### Complete Heap Monitoring Function
```cpp
// Source: ESP-IDF Heap Memory Debugging documentation
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/heap_debug.html

#include <esp_heap_caps.h>

void logHeapStats(char* buffer, size_t buf_size) {
    // Internal RAM stats (critical)
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    uint8_t internal_frag = (internal_free > 0) ?
        (100 - (internal_largest * 100 / internal_free)) : 0;

    // PSRAM stats
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    snprintf(buffer, buf_size,
        "HEAP: int_free=%u int_largest=%u int_min=%u int_frag=%u%% "
        "psram_free=%u psram_largest=%u",
        internal_free, internal_largest, internal_min, internal_frag,
        psram_free, psram_largest);
}
```

### Task Stack High Water Mark Iteration
```cpp
// Source: ESP-IDF FreeRTOS documentation
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/freertos_idf.html

void logTaskStacks(char* buffer, size_t buf_size) {
    size_t offset = 0;

    for (size_t i = 0; i < _task_count && offset < buf_size - 50; i++) {
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(_task_registry[i].handle);
        // hwm is in words (4 bytes on ESP32)
        offset += snprintf(buffer + offset, buf_size - offset,
            "%s=%u ", _task_registry[i].name, hwm * 4);
    }
}
```

### FreeRTOS Software Timer Creation
```cpp
// Source: ESP-IDF FreeRTOS documentation

#include <freertos/timers.h>

static TimerHandle_t _monitor_timer = nullptr;

bool initMonitorTimer(uint32_t interval_ms) {
    _monitor_timer = xTimerCreate(
        "mem_mon",                              // Timer name
        pdMS_TO_TICKS(interval_ms),            // Period in ticks
        pdTRUE,                                 // Auto-reload
        nullptr,                                // Timer ID (unused)
        memoryMonitorCallback                   // Callback function
    );

    if (_monitor_timer == nullptr) {
        return false;
    }

    return xTimerStart(_monitor_timer, 0) == pdPASS;
}

void memoryMonitorCallback(TimerHandle_t timer) {
    // This runs in timer daemon task context
    // Safe to do moderate work, but keep it brief
    static char buffer[256];
    logHeapStats(buffer, sizeof(buffer));
    Serial.println(buffer);
}
```

### SD Card Log File with Session Name
```cpp
// Existing project pattern - uses FileStream::MODE_APPEND

#include <FileSystem.h>
#include <Utilities/OS.h>

static RNS::FileStream _log_file = {RNS::Type::NONE};
static bool _sd_available = false;

bool openLogFile() {
    // Generate session-based filename using boot millis
    char filename[64];
    snprintf(filename, sizeof(filename), "/memory_%08lX.txt",
             (unsigned long)millis());

    _log_file = RNS::Utilities::OS::open_file(filename,
                                               RNS::FileStream::MODE_APPEND);
    if (!_log_file) {
        Serial.println("[MEM] SD card unavailable, serial only");
        _sd_available = false;
        return false;
    }

    // Write header
    const char* header = "# Memory Log - timestamp_ms,internal_free,internal_largest,"
                        "internal_frag,psram_free,psram_largest,tasks...\n";
    _log_file.write((const uint8_t*)header, strlen(header));
    _sd_available = true;
    return true;
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `ESP.getFreeHeap()` | `heap_caps_get_free_size()` | ESP-IDF v4.0+ | Per-region monitoring |
| Custom malloc wrappers | Built-in heap APIs | Always available | No overhead |
| `vTaskList()` string parsing | `uxTaskGetSystemState()` | ESP-IDF 4.x | Structured data (but needs config) |
| Manual timer in loop() | FreeRTOS xTimerCreate | FreeRTOS standard | Proper RTOS integration |

**Deprecated/outdated:**
- `ESP.getHeapSize()` / `ESP.getFreeHeap()`: Still work, but less detailed than heap_caps APIs
- `xPortGetFreeHeapSize()`: Low-level FreeRTOS API, prefer heap_caps wrapper

## Open Questions

Things that couldn't be fully resolved:

1. **SD Card Availability in T-Deck**
   - What we know: SPIFFS is used for persistence, UniversalFileSystem abstracts storage
   - What's unclear: Whether T-Deck has SD card slot and which pins/library to use
   - Recommendation: Check hardware documentation; fall back to SPIFFS if no SD card, or serial-only if SPIFFS is inappropriate for logs

2. **RTC Availability for Timestamps**
   - What we know: millis() is available, GPS library included
   - What's unclear: Whether RTC module present for persistent timestamps
   - Recommendation: Use millis-since-boot for log timestamps; real time is nice-to-have

3. **Exact Task Names in Application**
   - What we know: LVGL task exists ("lvgl"), BLE tasks exist
   - What's unclear: Complete list of FreeRTOS tasks and their names
   - Recommendation: Add logging at boot to discover task names, then hardcode registry

## Sources

### Primary (HIGH confidence)
- [ESP-IDF Heap Memory Debugging v5.5.1](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/system/heap_debug.html) - heap_caps API reference
- [ESP-IDF FreeRTOS IDF v5.5.1](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/system/freertos_idf.html) - Task APIs, timer APIs
- [ESP-IDF RAM Usage Guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/performance/ram-usage.html) - Measuring dynamic memory

### Secondary (MEDIUM confidence)
- [PlatformIO Community - FreeRTOS Trace Facility](https://community.platformio.org/t/how-to-enable-freertos-trace-facility-for-esp32s3/39869) - Arduino framework limitations
- [ESP32 Forum - uxTaskGetSystemState](https://www.esp32.com/viewtopic.php?t=3674) - Configuration requirements
- [Random Nerd Tutorials - ESP32 SD Card](https://randomnerdtutorials.com/esp32-microsd-card-arduino/) - SD card file operations

### Codebase (HIGH confidence - existing patterns)
- `/home/tyler/repos/public/microReticulum/src/Utilities/OS.cpp` - Existing heap_available(), heap_max_block() implementations
- `/home/tyler/repos/public/microReticulum/examples/lxmf_tdeck/src/main.cpp` lines 1280-1330 - Existing monitoring pattern
- `/home/tyler/repos/public/microReticulum/src/Log.h` - Build flag pattern (MEM_LOG)
- `/home/tyler/repos/public/microReticulum/examples/lxmf_tdeck/lib/universal_filesystem/` - File I/O abstraction

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - ESP-IDF official documentation, verified in codebase
- Architecture: HIGH - Patterns derived from existing codebase and official docs
- Pitfalls: HIGH - Verified through official docs and community reports

**Research date:** 2026-01-23
**Valid until:** 2026-02-23 (ESP-IDF APIs are stable)
