# ESP32-S3 Firmware Stability Audit: Debug & Analysis Tools

**Project:** microReticulum (T-Deck Plus LXMF Client)
**Platform:** ESP32-S3 with 8MB Flash, 8MB PSRAM
**Framework:** PlatformIO + Arduino + FreeRTOS
**Researched:** 2026-01-23
**Symptoms:** 15+ second boot time, crashes after extended runtime (hours/days)
**Suspected Cause:** Heap fragmentation

---

## Executive Summary

This stack recommendation covers tools and techniques for auditing ESP32-S3 firmware stability issues. The project uses PlatformIO with Arduino framework, which limits some ESP-IDF native tools but still provides access to the essential heap debugging APIs. Priority is given to runtime memory analysis and fragmentation detection, with secondary focus on boot profiling and static analysis.

---

## 1. Memory Analysis Tools

### 1.1 ESP-IDF Heap Debugging APIs (Runtime)

**Confidence:** HIGH (Official Espressif documentation)

These functions are accessible from Arduino framework and are the primary tools for heap analysis.

| Function | Purpose | When to Use |
|----------|---------|-------------|
| `heap_caps_get_free_size(MALLOC_CAP_DEFAULT)` | Total free heap bytes | Periodic monitoring |
| `heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)` | Largest contiguous free block | **Fragmentation indicator** |
| `heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT)` | Low watermark since boot | Identify worst-case usage |
| `heap_caps_print_heap_info(MALLOC_CAP_DEFAULT)` | Dump heap stats to serial | Debug sessions |
| `heap_caps_dump_all()` | Detailed block-level structure | Deep fragmentation analysis |

**Fragmentation Detection Formula:**
```cpp
size_t free = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
float fragmentation = 100.0f * (1.0f - ((float)largest / (float)free));
// >50% fragmentation is problematic
```

**Why This Matters:** Comparing `largest_free_block` to `total_free` directly reveals fragmentation. If you have 100KB free but largest block is 10KB, you have 90% fragmentation and cannot allocate anything larger than 10KB.

**Source:** [ESP-IDF Heap Memory Debugging v5.5.2](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/heap_debug.html)

### 1.2 PSRAM vs Internal RAM Monitoring

**Confidence:** HIGH

The T-Deck has 8MB PSRAM. Monitor both memory regions separately:

```cpp
// Internal SRAM (fast, limited ~320KB usable)
size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

// PSRAM (slower, 8MB)
size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
```

**Why This Matters:** Crashes may occur from internal RAM exhaustion even with ample PSRAM. LVGL buffers and BLE stacks prefer internal RAM.

### 1.3 Heap Corruption Detection

**Confidence:** HIGH

ESP-IDF provides three corruption detection levels, configurable via `sdkconfig` or menuconfig:

| Level | Config Option | Overhead | Detects |
|-------|---------------|----------|---------|
| Basic | Default (assertions enabled) | Minimal | Buffer overruns via structure validation |
| Light | `CONFIG_HEAP_CORRUPTION_DETECTION=1` | Low | Single-byte overruns via canary bytes |
| Comprehensive | `CONFIG_HEAP_CORRUPTION_DETECTION=2` | High | Use-after-free, uninitialized access |

**Comprehensive Mode Patterns:**
- Crash at address `0xCECECECE` = Uninitialized memory read
- Crash at address `0xFEFEFEFE` = Use-after-free

**Periodic Integrity Check:**
```cpp
// Call periodically to narrow down corruption timing
if (!heap_caps_check_integrity_all(true)) {
    Serial.println("HEAP CORRUPTION DETECTED");
}
```

**Source:** [ESP-IDF Heap Corruption Detection](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/heap_debug.html)

### 1.4 Heap Tracing (Leak Detection)

**Confidence:** MEDIUM (Requires sdkconfig modification, may need ESP-IDF framework)

Standalone heap tracing can identify memory leaks:

```cpp
#include "esp_heap_trace.h"

#define NUM_RECORDS 100
static heap_trace_record_t trace_record[NUM_RECORDS];

void setup() {
    heap_trace_init_standalone(trace_record, NUM_RECORDS);
}

void checkForLeaks() {
    heap_trace_start(HEAP_TRACE_LEAKS);
    // ... suspicious code path ...
    heap_trace_stop();
    heap_trace_dump();  // Prints allocations not freed
}
```

**Configuration Required:**
- `CONFIG_HEAP_TRACING_DEST=STANDALONE` or `HOST`
- `CONFIG_HEAP_TRACING_STACK_DEPTH=4` (call stack depth per record)

**Why This Matters:** Identifies exact allocation sites of leaked memory.

**Limitation:** Arduino framework may not expose these APIs without sdkconfig modifications. Verify with a test build.

### 1.5 Task-Specific Heap Tracking

**Confidence:** MEDIUM (Requires `CONFIG_HEAP_TASK_TRACKING=y`)

Track memory usage per FreeRTOS task:

```cpp
// Print memory usage by all tasks
heap_caps_print_all_task_stat_overview();

// Detailed breakdown for specific task
heap_caps_print_single_task_stat(xTaskGetHandle("BLETask"));
```

**Why This Matters:** Your architecture has dedicated tasks for LVGL, BLE, and Transport. This identifies which subsystem is the memory hog.

---

## 2. FreeRTOS Stack Analysis

### 2.1 Stack Overflow Detection

**Confidence:** HIGH

Enable via `configCHECK_FOR_STACK_OVERFLOW=2` in FreeRTOSConfig (usually in sdkconfig):

```cpp
// Hook called on stack overflow
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    Serial.printf("STACK OVERFLOW: Task '%s'\n", pcTaskName);
    abort();
}
```

**ESP-IDF Watchpoint Mode:** Enable `CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK` for hardware-assisted detection that catches overflows within 28 bytes of stack end.

**Source:** [ESP32 FreeRTOS Task Stack Management](https://controllerstech.com/esp32-freertos-task-priority-stack-management/)

### 2.2 Stack High Water Mark Monitoring

**Confidence:** HIGH

Monitor remaining stack space per task:

```cpp
void printStackUsage() {
    TaskHandle_t tasks[] = {lvglTaskHandle, bleTaskHandle, transportTaskHandle};
    const char* names[] = {"LVGL", "BLE", "Transport"};

    for (int i = 0; i < 3; i++) {
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(tasks[i]);
        Serial.printf("%s stack remaining: %u bytes\n", names[i], hwm * sizeof(StackType_t));
    }
}
```

**Recommended Stack Sizes (ESP32):**
- Minimal task: 2048 bytes
- Task with logging/printf: 4096 bytes
- BLE/Network tasks: 8192+ bytes
- LVGL task: 8192-16384 bytes (depends on UI complexity)

**Why This Matters:** Stack overflow can corrupt heap metadata, causing seemingly random heap corruption.

### 2.3 FreeRTOS Runtime Stats

**Confidence:** HIGH

Enable `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y`:

```cpp
void printTaskStats() {
    char buffer[1024];
    vTaskGetRunTimeStats(buffer);
    Serial.println(buffer);
}
```

**Why This Matters:** Identifies CPU-hogging tasks that may delay time-critical operations.

---

## 3. Boot Time Profiling

### 3.1 GPIO Timing Method

**Confidence:** HIGH (Hardware-based, most accurate)

```cpp
void setup() {
    gpio_set_direction(GPIO_NUM_X, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_X, 1);  // First line in setup()

    // ... rest of initialization ...
}
```

Connect GPIO to oscilloscope/logic analyzer. Trigger on EN pin rising edge. Measure time to GPIO high.

**Why This Matters:** Precise measurement of boot time independent of serial output timing.

### 3.2 esp_timer Instrumentation

**Confidence:** HIGH

```cpp
void setup() {
    int64_t boot_start = esp_timer_get_time();

    // Section timing
    int64_t t1 = esp_timer_get_time();
    initBLE();
    Serial.printf("BLE init: %lld us\n", esp_timer_get_time() - t1);

    t1 = esp_timer_get_time();
    initLVGL();
    Serial.printf("LVGL init: %lld us\n", esp_timer_get_time() - t1);

    // ... more sections ...

    Serial.printf("Total boot: %lld us\n", esp_timer_get_time() - boot_start);
}
```

### 3.3 Boot Time Configuration Optimizations

**Confidence:** HIGH (Official Espressif recommendations)

| Configuration | Impact | Trade-off |
|--------------|--------|-----------|
| `CONFIG_LOG_DEFAULT_LEVEL=1` (Error only) | Major | Less debug info |
| `CONFIG_BOOTLOADER_LOG_LEVEL=0` (None) | Moderate | No bootloader output |
| `CONFIG_SPIRAM_MEMTEST=n` | ~1 sec per 4MB | Risk of undetected bad PSRAM |
| `CONFIG_BOOTLOADER_SKIP_VALIDATE_ON_POWER_ON=y` | Moderate | Flash corruption risk |
| `CONFIG_RTC_CLK_CAL_CYCLES=0` | Small | Reduced RTC accuracy |
| Flash mode: QIO (already set) | Major | Already optimal |

**Unoptimized baseline:** ~300ms for bootloader alone
**With PSRAM test:** +2 seconds (8MB PSRAM)

**Source:** [ESP-IDF Speed Optimization](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/speed.html)

---

## 4. Real-Time Tracing

### 4.1 SEGGER SystemView

**Confidence:** MEDIUM (Requires setup, JTAG optional)

SystemView provides real-time visualization of FreeRTOS task execution, interrupts, and timing.

**Setup (ESP-IDF v5.5+):**
1. Add component: `idf.py add-dependency espressif/esp_sysview`
2. Enable: `CONFIG_APPTRACE_SV_ENABLE=y`
3. Select interface: JTAG or UART

**UART Mode:** Direct connection to SystemView desktop app without JTAG hardware.

**What You See:**
- Task timeline with execution periods
- Context switch timing
- Interrupt latency
- CPU load per task

**Why This Matters:** Identifies timing issues, priority inversions, and tasks blocking critical operations.

**Limitation:** Arduino framework integration may require additional configuration. JTAG mode most reliable.

**Source:** [ESP-IDF Application Level Tracing](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/app_trace.html)

### 4.2 PlatformIO Monitor Filter

**Confidence:** HIGH (Already configured in project)

Your `platformio.ini` already includes:
```ini
monitor_filters =
    esp32_exception_decoder
    time
```

The `esp32_exception_decoder` automatically decodes backtraces when crashes occur.

---

## 5. JTAG Debugging

### 5.1 ESP32-S3 Built-in USB JTAG

**Confidence:** HIGH

ESP32-S3 has built-in USB JTAG - no external probe needed.

**PlatformIO Configuration:**
```ini
[env:tdeck-debug]
extends = env:tdeck
build_type = debug
debug_tool = esp-builtin
debug_speed = 5000
upload_protocol = esp-builtin
```

**Capabilities:**
- Breakpoints
- Variable inspection
- Memory examination
- Call stack analysis

**Limitation:** SD card functionality conflicts with JTAG pins on some boards.

**Source:** [ESP-IDF JTAG Debugging ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/jtag-debugging/index.html)

### 5.2 GDB Commands for Memory Debugging

**Confidence:** HIGH

```gdb
# Examine heap
p heap_caps_get_free_size(0)
p heap_caps_get_largest_free_block(0)

# Dump memory region
x/100x 0x3FC00000

# Check task stacks
info threads
thread 2
bt
```

---

## 6. Core Dump Analysis

### 6.1 Flash-Based Core Dumps

**Confidence:** HIGH

Enable core dumps to flash for post-mortem analysis:

**Configuration:**
- `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`
- `CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y`

**Analysis:**
```bash
# From ESP-IDF
idf.py coredump-debug

# Or with PlatformIO (if esp-coredump tool available)
espcoredump.py info_corefile -t raw -c /path/to/core build/firmware.elf
```

**What You Get:**
- Full backtrace at crash
- Task states
- Variable values (if COREDUMP_DRAM_ATTR applied)

**Why This Matters:** Captures crash state for analysis even when device is unattended.

**Source:** [ESP-IDF Core Dump](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/core_dump.html)

---

## 7. Static Analysis

### 7.1 PlatformIO Check (Cppcheck + Clang-Tidy)

**Confidence:** HIGH

```bash
# Run static analysis
pio check --skip-packages -e tdeck

# Clang-tidy only
pio check --tool clangtidy --skip-packages -e tdeck

# Cppcheck only
pio check --tool cppcheck --skip-packages -e tdeck
```

**Useful Checks:**
- Memory leaks
- Buffer overflows
- Null pointer dereferences
- Style violations

**Limitation:** `--skip-packages` avoids warnings from library code but may miss issues at library boundaries.

**Source:** [PlatformIO Static Code Analysis](https://docs.platformio.org/en/latest/advanced/static-code-analysis/index.html)

### 7.2 IDF Size Tool

**Confidence:** MEDIUM (Requires ESP-IDF toolchain)

```bash
# From ESP-IDF build
idf.py size           # Summary
idf.py size-components  # Per-library breakdown
idf.py size-files       # Per-file breakdown
```

**PlatformIO Alternative:**
```bash
pio run -e tdeck -v  # Verbose output includes section sizes
```

**Why This Matters:** Identifies which components consume the most static RAM, leaving less for heap.

---

## 8. LVGL-Specific Monitoring

### 8.1 LVGL Memory Monitor Widget

**Confidence:** HIGH

Enable in `lv_conf.h`:
```c
#define LV_USE_MEM_MONITOR 1  // Requires LV_MEM_CUSTOM = 0
```

Displays memory usage and fragmentation in bottom-left corner.

### 8.2 LVGL Memory Configuration

**Confidence:** HIGH

Current settings to verify in `lv_conf.h`:

| Setting | Recommended | Why |
|---------|-------------|-----|
| `LV_MEM_CUSTOM` | 1 (use system heap) | Single heap, less fragmentation |
| `LV_MEM_SIZE` | If `LV_MEM_CUSTOM=0`, at least 64KB | LVGL internal heap size |
| `LV_MEM_ADR` | PSRAM address | Use PSRAM for LVGL heap |

**Source:** [LVGL Memory Handling](https://forum.lvgl.io/t/lvgl-memory-management-help/14046)

---

## 9. NimBLE Memory Optimization

### 9.1 Configuration Tuning

**Confidence:** HIGH

NimBLE uses ~149KB RAM with defaults (47KB IRAM + 14KB DRAM + 88KB heap).

Key optimizations in `sdkconfig` or menuconfig:

```
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2
CONFIG_BT_NIMBLE_MAX_BONDS=4
CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT=12
CONFIG_BT_NIMBLE_MSYS_1_BLOCK_SIZE=256
CONFIG_BTDM_CTRL_BLE_MAX_CONN=2
```

Disable unused roles:
```
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=n  # If not advertising
CONFIG_BT_NIMBLE_ROLE_OBSERVER=n     # If not scanning
```

**Expected Savings:** 20-40KB heap

**Source:** [ESP32 Forum - Reduce RAM usage of NimBLE](https://esp32.com/viewtopic.php?t=33783)

---

## 10. What NOT to Use

### 10.1 AddressSanitizer (ASan)

**Confidence:** HIGH

**Do Not Use on ESP32.** ASan requires 2-3x RAM overhead and is not supported on memory-constrained embedded targets.

**Alternative:** Use ESP-IDF heap corruption detection (Comprehensive mode) which provides similar detection with lower overhead.

**For thorough testing:** Run code on native Linux target with ASan enabled, then deploy to ESP32 with heap corruption detection.

### 10.2 Valgrind

**Confidence:** HIGH

**Not Available.** Valgrind is x86/x64 only. No embedded support.

**Alternative:** ESP-IDF heap tracing provides similar leak detection.

### 10.3 ESP-IDF MenuConfig Directly

**Confidence:** MEDIUM

With Arduino framework on PlatformIO, you cannot use `idf.py menuconfig` directly. Instead:
- Create `sdkconfig.defaults` file
- Use `board_build.arduino.config` in platformio.ini
- Or switch to ESP-IDF framework for full control

---

## Recommended Audit Workflow

### Phase 1: Baseline Measurement
1. Add heap monitoring to `loop()` - log every 60 seconds
2. Track: `free`, `largest_block`, `minimum_free_size`
3. Calculate fragmentation percentage
4. Run for 24+ hours, log to SD card or serial

### Phase 2: Boot Time Profiling
1. Add `esp_timer_get_time()` instrumentation to major init sections
2. Identify slowest components
3. Test config optimizations one at a time

### Phase 3: Crash Analysis
1. Enable core dumps to flash
2. Run until crash
3. Analyze with `espcoredump.py` or `idf.py coredump-debug`

### Phase 4: Static Analysis
1. Run `pio check --skip-packages`
2. Fix high-severity issues
3. Review memory allocation patterns

### Phase 5: Deep Dive (if needed)
1. Enable JTAG debugging
2. Set watchpoints on corrupted addresses
3. Use SystemView for timing analysis

---

## Sources

### Official Documentation (HIGH confidence)
- [ESP-IDF Heap Memory Debugging v5.5.2](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/heap_debug.html)
- [ESP-IDF Speed Optimization](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/speed.html)
- [ESP-IDF JTAG Debugging ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/jtag-debugging/index.html)
- [ESP-IDF Core Dump](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/core_dump.html)
- [ESP-IDF Application Level Tracing](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/app_trace.html)

### PlatformIO (HIGH confidence)
- [PlatformIO Static Code Analysis](https://docs.platformio.org/en/latest/advanced/static-code-analysis/index.html)
- [PlatformIO Debugging](https://docs.platformio.org/en/latest/plus/debugging.html)

### Community/Forums (MEDIUM confidence)
- [ESP32 Forum - Heap Fragmentation](https://esp32.com/viewtopic.php?t=5646)
- [ESP32 Forum - NimBLE RAM Usage](https://esp32.com/viewtopic.php?t=33783)
- [LVGL Forum - Memory Management](https://forum.lvgl.io/t/lvgl-memory-management-help/14046)
- [PlatformIO Community - ESP32-S3 Debugging](https://community.platformio.org/t/how-to-use-jtag-built-in-debugger-of-the-esp32-s3-in-platformio/36042)
