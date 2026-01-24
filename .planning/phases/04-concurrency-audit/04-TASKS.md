# FreeRTOS Tasks and Watchdog Audit

**Phase:** 04-concurrency-audit
**Date:** 2026-01-24
**Requirement References:** CONC-03, CONC-05

## Executive Summary

| Metric | Value |
|--------|-------|
| Total FreeRTOS Tasks | 3 (+1 timer daemon) |
| Timer-based Tasks | 1 (MemoryMonitor) |
| TWDT Status | **NOT CONFIGURED** |
| Issues Found | 5 (0 Critical, 2 High, 2 Medium, 1 Low) |

This audit inventories all FreeRTOS tasks in the microReticulum firmware, analyzes their yield patterns and blocking operations, and assesses stack size adequacy. The primary finding is that the **ESP32 Task Watchdog Timer (TWDT) is not explicitly configured**, leaving the system vulnerable to undetected task starvation or deadlock scenarios.

---

## Task Inventory

### FreeRTOS Tasks

| Task | Stack | Priority | Core | Created At | Purpose |
|------|-------|----------|------|------------|---------|
| `lvgl` | 8192 bytes | 2 | 1 | LVGLInit.cpp:181-191 | LVGL UI rendering and event processing |
| `ble` | 8192 bytes | Configurable | 0 | BLEInterface.cpp:916-924 | BLE mesh networking (scan, connect, GATT) |
| `loopTask` | 8192 bytes (default) | 1 | Any | Arduino framework | Main application loop (Reticulum, interfaces) |

**Task Details:**

1. **LVGL Task** (`lvgl`)
   - **Function:** `LVGLInit::lvgl_task()`
   - **Creation:** `xTaskCreatePinnedToCore()` with explicit core 1 affinity
   - **Stack:** 8192 bytes (8KB)
   - **Priority:** 2 (higher than default for smooth rendering)
   - **Purpose:** Runs `lv_task_handler()` continuously with mutex protection

2. **BLE Task** (`ble`)
   - **Function:** `ble_task()` wrapping `BLEInterface::loop()`
   - **Creation:** `xTaskCreatePinnedToCore()` pinned to core 0 (where BT controller runs)
   - **Stack:** 8192 bytes (8KB)
   - **Priority:** Configurable at runtime (typically 1)
   - **Purpose:** BLE mesh operations - scanning, connecting, GATT read/write

3. **Arduino Main Loop** (`loopTask`)
   - **Function:** Arduino `loop()` in main.cpp
   - **Creation:** Arduino framework (implicit)
   - **Stack:** 8192 bytes (ESP32-S3 default, not explicitly configured in platformio.ini)
   - **Priority:** 1 (Arduino default)
   - **Purpose:** Orchestrates all non-BLE processing:
     - `reticulum->loop()` - Transport processing
     - Interface loops (TCP, LoRa)
     - LXMF router queues
     - UI updates
     - GPS data reading
     - Periodic diagnostics

### Timer-Based Tasks

| Timer | Interval | Callback | Stack | Purpose |
|-------|----------|----------|-------|---------|
| `mem_mon` | 30000ms | `MemoryMonitor::timerCallback` | Timer daemon | Heap and stack monitoring |

**Timer Details:**

1. **Memory Monitor Timer** (`mem_mon`)
   - **Created:** `xTimerCreate()` in MemoryMonitor.cpp:54
   - **Interval:** 30 seconds (configurable at init)
   - **Auto-reload:** Yes (pdTRUE)
   - **Callback:** Logs heap stats and task stack high water marks
   - **Stack:** Runs in FreeRTOS timer daemon task (shared stack)

### NimBLE Internal Tasks

The NimBLE stack creates its own internal tasks (not directly controllable):

| Task | Stack | Notes |
|------|-------|-------|
| `nimble_host` | 8192 bytes (CONFIG_BT_NIMBLE_TASK_STACK_SIZE) | NimBLE host task, handles all BLE callbacks |

---

## Yield Pattern Analysis

| Task | Yield Method | Interval | Blocking Ops | Risk Level |
|------|--------------|----------|--------------|------------|
| `lvgl` | `vTaskDelay(pdMS_TO_TICKS(5))` | 5ms | `xSemaphoreTakeRecursive(..., portMAX_DELAY)` | **Medium** |
| `ble` | `vTaskDelay(pdMS_TO_TICKS(10))` | 10ms | Mutex acquisition in loop | **Low** |
| `loopTask` | `delay(5)` | 5ms | Various interface I/O | **Medium** |
| `LXStamper` (in loopTask) | `vTaskDelay(1)` every 100 rounds | ~1 tick/100 rounds | CPU-intensive crypto | **High** |

### Yield Pattern Details

1. **LVGL Task**
   - Yields every 5ms via `vTaskDelay()`
   - LVGL recommends 5-10ms between `lv_task_handler()` calls
   - **Blocking:** Takes recursive mutex with `portMAX_DELAY` (indefinite wait)
   - **Risk:** If another thread holds mutex for extended period, LVGL stalls

2. **BLE Task**
   - Yields every 10ms via `vTaskDelay()`
   - Internal mutex protection in `BLEInterface::loop()`
   - NimBLEPlatform uses multiple `delay(10-100)` calls for timing
   - **Blocking:** Various 10-100ms delays during BLE operations
   - **Risk:** Low - delays are explicit and short

3. **Main Loop (Arduino)**
   - Yields every 5ms via `delay(5)` at end of loop
   - Multiple subsystem loops called sequentially
   - **Blocking:**
     - TCP interface may block on network I/O
     - GPS serial read is non-blocking (polling)
     - Interface timeouts handled internally
   - **Risk:** Medium - long processing chains without intermediate yields

4. **LXStamper (CPU-intensive)**
   - Called from main task during stamp generation
   - Yields `vTaskDelay(1)` every 100 rounds (lines 195-197)
   - **Blocking:** Intensive SHA256/HKDF computations
   - **Risk:** **High** - stamp generation can take minutes, yields only every 100 hashes

### Blocking Operations Inventory

| Location | Type | Timeout | Risk |
|----------|------|---------|------|
| LVGLInit.cpp:157 | `xSemaphoreTakeRecursive(_mutex, portMAX_DELAY)` | Infinite | High |
| BLEInterface.cpp (loop) | Internal mutex | Short | Low |
| NimBLEPlatform.cpp:837 | `xSemaphoreTake(_conn_mutex, pdMS_TO_TICKS(100))` | 100ms | Low |
| SX1262Interface.cpp:72 | `xSemaphoreTake(_spi_mutex, pdMS_TO_TICKS(1000))` | 1000ms | Medium |
| Tone.cpp:100 | `i2s_write(..., portMAX_DELAY)` | Infinite | Medium |
| LXStamper.cpp (loop) | CPU-bound crypto | N/A | High |
| Display.cpp | Multiple `delay()` calls | 10-150ms | Low |

---

## Stack Size Assessment

### Phase 1 Monitoring Cross-Reference

The MemoryMonitor (Phase 1) tracks stack high water marks with these parameters:
- **Warning threshold:** 256 bytes remaining (MemoryMonitor.cpp:243)
- **Monitored tasks:** LVGL task registered in main.cpp:525
- **Interval:** 30 seconds

### Stack Usage Estimates

| Task | Allocated | Est. Peak Usage | Est. Margin | Risk |
|------|-----------|-----------------|-------------|------|
| `lvgl` | 8192 bytes | ~5000 bytes | ~3000 bytes | **Low** |
| `ble` | 8192 bytes | ~6000 bytes | ~2000 bytes | **Medium** |
| `loopTask` | 8192 bytes | ~5500 bytes | ~2500 bytes | **Medium** |

**Usage Estimation Rationale:**

1. **LVGL Task (Est. 5000 bytes)**
   - LVGL widget rendering requires moderate stack for style calculations
   - Recursive mutex operations add stack frames
   - No deep callback nesting
   - 8KB allocation appears adequate

2. **BLE Task (Est. 6000 bytes)**
   - BLE callbacks can nest deeply (GAP -> GATT -> crypto)
   - NimBLE cryptographic operations use ~2KB stack
   - Service discovery allocates temporary buffers
   - **Recommendation:** Monitor HWM, consider 10KB if issues observed

3. **Main Loop Task (Est. 5500 bytes)**
   - Reticulum processing has moderate call depth
   - Crypto operations (Identity, packet handling) are stack-intensive
   - LXStamper stamp generation uses additional stack
   - Multiple interface loops add cumulative depth
   - **Recommendation:** Monitor HWM during stamp generation

### Stack Monitoring Output (Expected)

When MEMORY_INSTRUMENTATION_ENABLED is defined, periodic output:
```
[STACK] lvgl=XXXX
```

If any task shows < 256 bytes remaining:
```
[STACK] Task 'lvgl' stack low: XXX bytes remaining
```

---

## Watchdog Status

### Current TWDT Configuration

**Status: NOT EXPLICITLY CONFIGURED**

Analysis of `sdkconfig.defaults`:
- No `CONFIG_ESP_TASK_WDT*` entries present
- Uses ESP-IDF/Arduino framework defaults
- Default behavior for ESP32-S3 with Arduino:
  - TWDT is enabled by default
  - Only idle task is subscribed
  - User tasks are NOT automatically subscribed

**Evidence:** Grep for `CONFIG_ESP_TASK_WDT` in project files returns no results in project configuration.

### Current Watchdog Gaps

1. **LVGL task** - Not subscribed to TWDT
2. **BLE task** - Not subscribed to TWDT
3. **Main loop task** - Not subscribed to TWDT (Arduino handles this partially)

### Application-Level Watchdog Patterns

The codebase has incomplete application-level timeout mechanisms:

1. **Link::start_watchdog()** (src/Link.cpp:750-755)
   ```cpp
   // CBA TODO Implement watchdog
   void Link::start_watchdog() {
       //z thread = threading.Thread(target=_object->___watchdog_job)
       //z thread.daemon = True
       //z thread.start()
   }
   ```
   **Status:** Not implemented (Python code commented out)

2. **Resource watchdog_lock** (src/Resource.cpp:563-582)
   ```cpp
   if (_object->_watchdog_lock) {
       return;
   }
   _object->_watchdog_lock = true;
   // ... timeout checks ...
   _object->_watchdog_lock = false;
   ```
   **Status:** Implemented as reentrancy guard, not a true watchdog

### TWDT Recommendation

**Recommended Configuration:**

Add to `sdkconfig.defaults`:
```
# Task Watchdog Timer Configuration
CONFIG_ESP_TASK_WDT=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y
```

**Tasks to subscribe:**
1. LVGL task (critical for UI responsiveness)
2. BLE task (critical for mesh connectivity)
3. Main loop task (implicit via idle task monitoring)

**Implementation:**
```cpp
#include "esp_task_wdt.h"

// In LVGLInit::start_task() after task creation:
esp_task_wdt_add(_task_handle);

// In lvgl_task loop, after vTaskDelay:
esp_task_wdt_reset();
```

---

## Findings

### TASK-01: TWDT Not Configured (High)

**Severity:** High
**Location:** sdkconfig.defaults, task creation sites
**Description:** The ESP32 Task Watchdog Timer is not explicitly configured. Application tasks (LVGL, BLE, main) are not subscribed to TWDT, meaning task starvation or deadlock would go undetected until manual reset.

**Code Snippet:**
```cpp
// LVGLInit.cpp:181 - No TWDT subscription
result = xTaskCreatePinnedToCore(
    lvgl_task,
    "lvgl",
    8192,
    nullptr,
    priority,
    &_task_handle,
    core
);
// Missing: esp_task_wdt_add(_task_handle);
```

**Recommended Fix:**
1. Add TWDT configuration to sdkconfig.defaults
2. Subscribe LVGL and BLE tasks to TWDT
3. Add `esp_task_wdt_reset()` calls in task loops

---

### TASK-02: LXStamper CPU Hogging (High)

**Severity:** High
**Location:** src/LXMF/LXStamper.cpp:195-197
**Description:** Stamp generation is CPU-intensive and only yields every 100 rounds (approximately every 100 SHA256 hashes). During stamp generation, which can take minutes, other tasks on the same core may be starved.

**Code Snippet:**
```cpp
// Yield to allow other tasks (LVGL, network) to run
// This prevents UI freeze during stamp generation
#ifdef ESP_PLATFORM
if (rounds % 100 == 0) {
    vTaskDelay(1);  // Yield for 1 tick
}
#endif
```

**Recommended Fix:**
- Increase yield frequency to every 10-20 rounds
- Consider running stamp generation on dedicated low-priority task
- Add TWDT reset if subscribed

---

### TASK-03: Indefinite Mutex Wait in LVGL (Medium)

**Severity:** Medium
**Location:** src/UI/LVGL/LVGLInit.cpp:157
**Description:** LVGL task uses `portMAX_DELAY` when acquiring the recursive mutex. If another thread holds the mutex for an extended period, LVGL rendering stops indefinitely.

**Code Snippet:**
```cpp
while (true) {
    // Acquire mutex before calling LVGL
    if (xSemaphoreTakeRecursive(_mutex, portMAX_DELAY) == pdTRUE) {
        lv_task_handler();
        xSemaphoreGiveRecursive(_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(5));
}
```

**Recommended Fix:**
- Use a reasonable timeout (e.g., 100ms) instead of `portMAX_DELAY`
- Log warning if timeout occurs
- Continue loop to avoid complete stall

---

### TASK-04: Audio I2S Blocking Write (Medium)

**Severity:** Medium
**Location:** examples/lxmf_tdeck/lib/tone/Tone.cpp:100,105,118
**Description:** Audio playback uses `portMAX_DELAY` for I2S writes, which blocks indefinitely if I2S buffer is full.

**Code Snippet:**
```cpp
i2s_write(I2S_PORT, samples, sizeof(samples), &bytes_written, portMAX_DELAY);
```

**Recommended Fix:**
- Use a timeout (e.g., 1000ms) for I2S writes
- Handle timeout case gracefully (skip sample)

---

### TASK-05: Incomplete Link Watchdog (Low)

**Severity:** Low
**Location:** src/Link.cpp:750-755
**Description:** The Link watchdog mechanism is marked as TODO and not implemented. This is a direct port from Python that hasn't been completed.

**Code Snippet:**
```cpp
// CBA TODO Implement watchdog
void Link::start_watchdog() {
    //z thread = threading.Thread(target=_object->___watchdog_job)
    //z thread.daemon = True
    //z thread.start()
}
```

**Recommended Fix:**
- Implement using FreeRTOS software timer instead of thread
- Or integrate with Link::loop() as periodic check

---

## ASCII Diagram: Task Architecture

```
                           ESP32-S3 Dual Core
    +----------------------------------------------------------+
    |                                                          |
    |  CORE 0                          CORE 1                  |
    |  +----------------------+        +----------------------+|
    |  |                      |        |                      ||
    |  |  +----------------+  |        |  +----------------+  ||
    |  |  | BLE Task       |  |        |  | LVGL Task      |  ||
    |  |  | Pri: 1         |  |        |  | Pri: 2         |  ||
    |  |  | Stack: 8KB     |  |        |  | Stack: 8KB     |  ||
    |  |  |                |  |        |  |                |  ||
    |  |  | - Scan         |  |        |  | - lv_task_     |  ||
    |  |  | - Connect      |  |        |  |   handler()    |  ||
    |  |  | - GATT ops     |  |        |  | - Event proc   |  ||
    |  |  +-------+--------+  |        |  +-------+--------+  ||
    |  |          |           |        |          |           ||
    |  |          v           |        |          v           ||
    |  |  +----------------+  |        |  +----------------+  ||
    |  |  | NimBLE Host    |  |        |  | LVGL Mutex     |  ||
    |  |  | (internal)     |  |        |  | (recursive)    |  ||
    |  |  +----------------+  |        |  +----------------+  ||
    |  |                      |        |                      ||
    |  +----------------------+        +----------------------+|
    |                                                          |
    |  CORE 0 or 1 (not pinned)                               |
    |  +------------------------------------------------------+|
    |  | Main Loop Task (Arduino loopTask) Pri: 1             ||
    |  | Stack: 8KB                                            ||
    |  |                                                       ||
    |  |  +----------+ +----------+ +----------+ +----------+ ||
    |  |  |Reticulum | | TCP      | | LoRa     | | LXMF     | ||
    |  |  |  loop()  | |  loop()  | |  loop()  | |  router  | ||
    |  |  +----------+ +----------+ +----------+ +----------+ ||
    |  |                                                       ||
    |  |  +----------+ +----------+ +----------+              ||
    |  |  |   UI     | |   GPS    | |  Diag    |              ||
    |  |  | manager  | |  serial  | |  heap    |              ||
    |  |  +----------+ +----------+ +----------+              ||
    |  +------------------------------------------------------+|
    |                                                          |
    |  TIMER DAEMON TASK                                      |
    |  +------------------------------------------------------+|
    |  | mem_mon timer (30s) -> MemoryMonitor::timerCallback  ||
    |  +------------------------------------------------------+|
    |                                                          |
    +----------------------------------------------------------+

    SHARED RESOURCES:
    +----------------------------------------------------------+
    | LVGL Mutex (recursive)  <-- UI access from any task     |
    | BLE Mutex              <-- BLEInterface callback data   |
    | NimBLE Conn Mutex      <-- Connection map (100ms timeout)|
    | SPI Mutex              <-- LoRa radio access            |
    | NimBLE Spinlocks (76x) <-- State machine transitions    |
    +----------------------------------------------------------+
```

---

## Summary

### Issue Counts by Severity

| Severity | Count | Issues |
|----------|-------|--------|
| Critical | 0 | - |
| High | 2 | TASK-01 (TWDT), TASK-02 (LXStamper) |
| Medium | 2 | TASK-03 (LVGL mutex), TASK-04 (I2S blocking) |
| Low | 1 | TASK-05 (Link watchdog TODO) |

### Key Recommendations

1. **Enable TWDT** - Add TWDT configuration and subscribe critical tasks
2. **Fix LXStamper yields** - Increase yield frequency during stamp generation
3. **Add mutex timeouts** - Replace `portMAX_DELAY` with reasonable timeouts
4. **Monitor stack HWM** - Use Phase 1 instrumentation to validate estimates

### CONC-03 Status: Partially Met

- All tasks documented with yield patterns
- TWDT not configured (needs implementation)
- Blocking operations identified

### CONC-05 Status: Met

- All task stack sizes documented
- Estimates provided based on operation complexity
- Phase 1 monitoring infrastructure exists for validation
