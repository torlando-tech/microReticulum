# Concurrency Guide

This document describes the threading model, mutex usage, and synchronization patterns in microReticulum.

## Threading Model

microReticulum runs on FreeRTOS with the following primary tasks:

| Task | Priority | Core | Purpose |
|------|----------|------|---------|
| LVGL UI Task | 5 | 1 | UI rendering and event handling |
| Transport Task | 4 | 0 | Packet processing and routing |
| WiFi Task | 3 | 0 | TCP interface management |
| BLE Task | 3 | 0 | BLE interface and scanning |

## Mutexes

### LVGL Mutex

- **Purpose**: Protects all LVGL widget operations
- **Header**: `src/UI/LVGL/LVGLLock.h`
- **Type**: Recursive mutex (allows nested locking from same task)
- **Timeout**: 5 seconds in debug builds (assert on timeout), portMAX_DELAY in release

**When to use**:
- Any code that calls lv_* functions outside the LVGL task
- Screen constructors and destructors
- Widget update methods (show, hide, refresh)

**Do NOT use**:
- Event callbacks (already run within LVGL task handler with mutex held)
- Code already running in LVGL task

**Usage pattern**:
```cpp
#include "UI/LVGL/LVGLLock.h"

void MyScreen::refresh() {
    LVGL_LOCK();  // RAII guard - unlocks on scope exit
    lv_label_set_text(label, "Updated");
}
```

**Implementation details** (from LVGLLock.h):
```cpp
class LVGLLock {
public:
    LVGLLock() {
        SemaphoreHandle_t mutex = LVGLInit::get_mutex();
        if (mutex) {
#ifndef NDEBUG
            // Debug builds: 5-second timeout for deadlock detection
            BaseType_t result = xSemaphoreTakeRecursive(mutex, pdMS_TO_TICKS(5000));
            if (result != pdTRUE) {
                assert(false && "LVGL mutex timeout (5s) - possible deadlock");
            }
            _acquired = true;
#else
            // Release builds: Wait indefinitely
            xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
            _acquired = true;
#endif
        }
    }
    // ... destructor releases with xSemaphoreGiveRecursive ...
};
```

### BLE Connection Mutex

- **Purpose**: Protects BLE connection map and discovered devices cache
- **Header**: `src/BLE/platforms/NimBLEPlatform.h`
- **Member**: `_conn_mutex`
- **Type**: Binary semaphore (SemaphoreHandle_t)
- **Timeout**: 100ms for cache operations, varies for connection operations

**When to use**:
- Accessing `_connections` map
- Accessing `_discovered_devices` map
- During connect/disconnect operations

**Error handling**:
- Log warning on timeout (don't crash)
- Return gracefully without completing operation

**Example usage**:
```cpp
if (xSemaphoreTake(_conn_mutex, pdMS_TO_TICKS(100))) {
    auto cachedIt = _discovered_devices.find(addrKey);
    if (cachedIt != _discovered_devices.end()) {
        // Process cached device
        _discovered_devices.erase(cachedIt);
    }
    xSemaphoreGive(_conn_mutex);
} else {
    WARNING("NimBLEPlatform: conn_mutex timeout (100ms) during cache update");
}
```

**Related state machine protection**:
- BLE state variables (`_master_state`, `_slave_state`, `_gap_state`) use a spinlock (`portMUX_TYPE _state_mux`)
- Atomic state transitions via `transitionMasterState()`, `transitionSlaveState()`, `transitionGAPState()`

### Transport Data Structures

- **Purpose**: Protects packet queues, routing tables, and link management
- **Header**: `src/Transport.h`
- **Type**: Fixed-size pools with slot-based allocation (no dynamic heap allocation)

**Design**: Transport uses fixed-size pool arrays instead of STL containers with mutexes. This eliminates heap fragmentation and provides deterministic memory usage.

**Pool structures** (all static):
| Pool | Size | Purpose |
|------|------|---------|
| `_announce_table_pool` | 8 | Announces awaiting retransmission |
| `_destination_table_pool` | 16 | Next-hop routing information |
| `_reverse_table_pool` | 8 | Packet hash to return proofs |
| `_link_table_pool` | 8 | Active link hops |
| `_held_announces_pool` | 8 | Temporarily held announces |
| `_tunnels_pool` | 16 | Tunnels to other transports |
| `_receipts_pool` | 8 | Outgoing packet receipts |
| `_packet_hashlist_buffer` | 64 | Duplicate detection (circular) |

**Access pattern**:
```cpp
// Find existing slot
DestinationTableSlot* slot = find_destination_table_slot(hash);
if (slot) {
    // Process existing entry
}

// Allocate new slot
DestinationTableSlot* empty = find_empty_destination_table_slot();
if (empty) {
    empty->in_use = true;
    empty->destination_hash = hash;
    empty->entry = entry;
} else {
    // Pool full - handle gracefully (drop/log/cull)
}
```

**Overflow behavior**: When pools are full, `find_empty_*_slot()` returns nullptr. Callers must handle this gracefully (typically by dropping the operation and logging).

## Lock Ordering

To prevent deadlocks, acquire locks in this order:

```
1. Transport operations (pool slot access)
2. BLE connection mutex (_conn_mutex)
3. LVGL mutex (via LVGL_LOCK())
```

**Never** acquire:
- Transport pool access while holding LVGL mutex
- BLE mutex while holding LVGL mutex

This ordering ensures that:
- Lower-level (transport/BLE) operations complete before UI updates
- UI code can safely call transport/BLE APIs without deadlock

**Rationale**: UI tasks (LVGL) have highest priority but should hold locks for shortest duration. By acquiring UI locks last, we minimize the time high-priority tasks are blocked.

## LVGL Thread-Safety Rules

LVGL is single-threaded by design. All widget operations must be protected.

### Safe Patterns

1. **Constructor/Destructor**: Use LVGL_LOCK()
2. **Public methods called from other tasks**: Use LVGL_LOCK()
3. **Event callbacks**: Do NOT add lock (already protected)

### Unsafe Patterns

1. **Calling lv_* from ISR**: Never safe
2. **Nested LVGL_LOCK**: Safe (recursive mutex), but avoid if possible
3. **Long operations while holding lock**: Blocks UI rendering

### Screen Lifecycle

```cpp
MyScreen::MyScreen() {
    LVGL_LOCK();  // Required - constructor may run from any task
    _screen = lv_obj_create(lv_scr_act());
    // ... create widgets
}

MyScreen::~MyScreen() {
    LVGL_LOCK();  // Required - destructor may run from any task
    if (_screen) {
        lv_obj_del(_screen);
    }
}

void MyScreen::on_button_clicked(lv_event_t* event) {
    // No LVGL_LOCK needed - already in LVGL task context
    lv_label_set_text(label, "Clicked");
}
```

### Correct Screen Classes (reference implementations)

These screens already follow the LVGL_LOCK pattern correctly:
- `ChatScreen.cpp` - LVGL_LOCK in constructor and destructor
- `SettingsScreen.cpp` - LVGL_LOCK in constructor and destructor
- `ComposeScreen.cpp` - LVGL_LOCK in constructor and destructor
- `AnnounceListScreen.cpp` - LVGL_LOCK in constructor and destructor

## Debug vs Release Behavior

| Component | Debug Build | Release Build |
|-----------|-------------|---------------|
| LVGL mutex | 5s timeout + assert | portMAX_DELAY (infinite) |
| BLE cache mutex | 100ms timeout + log | 100ms timeout + log |
| I2S writes | 2s timeout + log | 2s timeout |

Debug builds crash-on-deadlock to surface issues during development.
Release builds continue operation, logging warnings for investigation.

**Debug detection**: The LVGL mutex uses `#ifndef NDEBUG` to detect debug builds. When `NDEBUG` is not defined (debug build), the 5-second timeout is enabled.

## Common Pitfalls

### 1. Forgetting LVGL_LOCK in Destructor
**Problem**: Screen deleted while LVGL task is rendering
**Solution**: Always LVGL_LOCK in destructor before lv_obj_del

### 2. LVGL_LOCK in Event Callback
**Problem**: Potential double-acquire (even with recursive mutex, it's wasteful)
**Solution**: Event callbacks run within lv_task_handler - no lock needed

### 3. Long Operations While Holding Lock
**Problem**: UI becomes unresponsive
**Solution**: Copy data, release lock, process, reacquire for update

### 4. Accessing BLE Cache After Timeout
**Problem**: Stale data if mutex acquire failed
**Solution**: Check return value, handle timeout gracefully

### 5. Transport Pool Overflow
**Problem**: Pool full, operation silently dropped
**Solution**: Check `find_empty_*_slot()` return value, implement proper culling

## Task Priorities

Higher numbers = higher priority in FreeRTOS.

| Priority | Tasks | Rationale |
|----------|-------|-----------|
| 5 | LVGL | UI responsiveness |
| 4 | Transport | Packet processing |
| 3 | WiFi, BLE | Network I/O |
| 2 | Background | Non-critical processing |
| 1 | Idle | System idle task |

## BLE State Machine

The BLE subsystem uses a dual-role state machine with three independent states:

| State Type | Enum | Purpose |
|------------|------|---------|
| Master | `MasterState` | Scanning/connecting (Central role) |
| Slave | `SlaveState` | Advertising (Peripheral role) |
| GAP | `GAPState` | Overall BLE subsystem coordination |

**Master states**: IDLE, SCAN_STARTING, SCANNING, SCAN_STOPPING, CONN_STARTING, CONNECTING, CONN_CANCELING

**Slave states**: IDLE, ADV_STARTING, ADVERTISING, ADV_STOPPING

**GAP states**: UNINITIALIZED, INITIALIZING, READY, MASTER_PRIORITY, SLAVE_PRIORITY, TRANSITIONING, ERROR_RECOVERY

**Transition pattern**:
```cpp
// Atomic compare-and-swap
bool transitionMasterState(MasterState expected, MasterState new_state);
bool transitionSlaveState(SlaveState expected, SlaveState new_state);
bool transitionGAPState(GAPState expected, GAPState new_state);
```

## BLE Discovered Device Cache

- **Maximum size**: 16 devices
- **Eviction policy**: Insertion-order based (FIFO, oldest non-connected first)
- **Connected device protection**: Devices in `_connections` are never evicted

**Implementation**:
```cpp
// Tracking structures
std::map<std::string, NimBLEAdvertisedDevice> _discovered_devices;
std::vector<std::string> _discovered_order;  // Insertion order

// Eviction when full
while (_discovered_devices.size() >= MAX_DISCOVERED_DEVICES) {
    // Find oldest non-connected device
    for (auto it = _discovered_order.begin(); it != _discovered_order.end(); ++it) {
        if (!isDeviceConnected(*it)) {
            _discovered_devices.erase(*it);
            _discovered_order.erase(it);
            break;
        }
    }
}
```

## I2S Audio Timeout

I2S writes use a 2-second timeout to prevent indefinite blocking:

```cpp
esp_err_t err = i2s_write(I2S_PORT, samples, sizeof(samples), &bytes_written,
                          pdMS_TO_TICKS(2000));  // 2 second timeout
if (err != ESP_OK || bytes_written == 0) {
    WARNING("I2S write timeout or error, skipping samples");
}
```

## Timing and Volatile Reference

This table summarizes all timing-sensitive delays and volatile variables in the codebase.
See individual code sites for detailed rationale comments (search for `DELAY RATIONALE` and `VOLATILE RATIONALE`).

### Delay Sites

| File | Line | Value | Purpose |
|------|------|-------|---------|
| NimBLEPlatform.cpp | 257 | 100ms | Stack init settling time (pre-reboot) |
| NimBLEPlatform.cpp | 288 | 100ms | Advertising restart after recovery |
| NimBLEPlatform.cpp | 408 | 10ms | Advertising stop polling interval |
| NimBLEPlatform.cpp | 442 | 10ms | Slave state polling |
| NimBLEPlatform.cpp | 506 | 50ms | Error recovery before retry |
| NimBLEPlatform.cpp | 521 | 50ms | Connect attempt recovery |
| NimBLEPlatform.cpp | 598 | 20ms | Stack settling before scan start |
| NimBLEPlatform.cpp | 682 | 10ms | Scan stop polling |
| NimBLEPlatform.cpp | 780 | 20ms | Service discovery settling |
| NimBLEPlatform.cpp | 796 | 10ms | Service discovery polling |
| NimBLEPlatform.cpp | 980 | 10ms | Host sync polling |
| NimBLEPlatform.cpp | 1061 | 50ms | Soft reset processing |
| NimBLEPlatform.cpp | 1072 | 10ms | Reset wait polling |
| NimBLEPlatform.cpp | 1086 | 10ms | Stack stabilization after cancel |
| NimBLEPlatform.cpp | 1330 | 10ms | Loop iteration throttle |
| Display.cpp | 104 | 150ms | LCD reset pulse width (ST7789 spec) |
| Display.cpp | 111,128,133,138 | 10ms | SPI command settling |

### Volatile Variables

| File | Lines | Type | Purpose |
|------|-------|------|---------|
| NimBLEPlatform.h | 299-301 | bool/int | Async connect callback flags (NimBLEClientCallbacks) |
| NimBLEPlatform.h | 310-313 | bool/int/uint16 | Native GAP connect callback flags (nativeGapEventHandler) |
| Trackball.h | 102-106 | int16/uint32 | ISR pulse counters (IRAM_ATTR handlers) |

**Timing design principles:**
- 10ms: NimBLE scheduler tick interval (minimum useful polling)
- 20ms: Brief settling (2 scheduler ticks for internal state)
- 50ms: Error recovery (5 scheduler ticks for stack processing)
- 100ms: State transition settling (advertising, initialization)
- 150ms+: Hardware specs (display reset per ST7789 datasheet)

## References

- [FreeRTOS Mutex Documentation](https://www.freertos.org/Real-time-operating-system-tutorial.html)
- [LVGL Threading](https://docs.lvgl.io/master/porting/os.html)
- [ESP-IDF FreeRTOS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos.html)
