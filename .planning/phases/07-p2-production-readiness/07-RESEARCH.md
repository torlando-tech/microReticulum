# Phase 7: P2 Production Readiness - Research

**Researched:** 2026-01-24
**Domain:** Memory allocation patterns, LVGL thread-safety, BLE cache management, mutex documentation
**Confidence:** HIGH

## Summary

Phase 7 addresses all P2 medium-severity issues identified in the v1 stability audits. The work spans four distinct subsystems: memory allocation patterns in core classes (Bytes, PacketReceipt, Persistence), LVGL thread-safety in UI screen classes, BLE discovered devices cache management, and infrastructure improvements (audio timeouts, mutex documentation).

The research confirms that all required changes are well-understood with clear implementation paths. The codebase already has established patterns for each concern (LVGL_LOCK usage, make_shared examples, JsonDocument API usage), making this primarily a matter of extending existing patterns to additional locations.

**Primary recommendation:** Implement changes incrementally by subsystem, starting with the simplest (ArduinoJson migration), then LVGL locking, then allocation patterns, and finally documentation. Each change is low-risk and isolated.

## Standard Stack

This phase uses existing libraries and patterns already in the codebase. No new dependencies are required.

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| ArduinoJson | 7.4.2 | JSON serialization | Already in use, v7 API is current |
| FreeRTOS | ESP-IDF bundled | Mutex/semaphore primitives | Platform standard |
| LVGL | 8.3.x | UI library | Already integrated |
| std::shared_ptr | C++11 | Smart pointer with make_shared | Language standard |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| esp_task_wdt.h | ESP-IDF | Watchdog timer debugging | Debug builds only |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Insertion-order eviction | LRU with timestamps | CONTEXT.md specifies insertion-order (simpler) |
| CONCURRENCY.md | CONTRIBUTING.md section | CONTEXT.md specifies dedicated file |

## Architecture Patterns

### Pattern 1: LVGL Lock in Constructor/Destructor

**What:** Add LVGL_LOCK() RAII guard at the start of constructors and destructors that call lv_* functions.

**When to use:** Any screen class that creates or destroys LVGL widgets.

**Example (from ChatScreen.cpp - already correct):**
```cpp
// Source: src/UI/LXMF/ChatScreen.cpp:21-51
ChatScreen::ChatScreen(lv_obj_t* parent)
    : _screen(nullptr), _header(nullptr), _message_list(nullptr), ... {
    LVGL_LOCK();  // First line after initializer list

    if (parent) {
        _screen = lv_obj_create(parent);
    } else {
        _screen = lv_obj_create(lv_scr_act());
    }
    // ... more lv_* calls ...
}

ChatScreen::~ChatScreen() {
    LVGL_LOCK();  // First line
    if (_screen) {
        lv_obj_del(_screen);
    }
}
```

**Files needing this pattern:**
- `src/UI/LXMF/SettingsScreen.cpp` - Constructor (line 55) and Destructor (line 105)
- `src/UI/LXMF/ComposeScreen.cpp` - Constructor and Destructor
- `src/UI/LXMF/AnnounceListScreen.cpp` - Constructor and Destructor

### Pattern 2: Debug Timeout for LVGL Mutex

**What:** In debug builds, use a 5-second timeout on LVGL mutex acquisition with assert on failure. In release builds, use portMAX_DELAY.

**When to use:** Modify the LVGLLock class constructor.

**Example:**
```cpp
// Source: src/UI/LVGL/LVGLLock.h (modification)
LVGLLock() {
    SemaphoreHandle_t mutex = LVGLInit::get_mutex();
    if (mutex) {
#ifdef DEBUG
        // Debug builds: 5s timeout to detect deadlocks
        if (xSemaphoreTakeRecursive(mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
            // Deadlock detected - crash with diagnostic
            ERROR("LVGL mutex acquisition timeout (5s) - possible deadlock");
            assert(false && "LVGL mutex deadlock detected");
        }
        _acquired = true;
#else
        // Release builds: infinite wait
        xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
        _acquired = true;
#endif
    }
}
```

### Pattern 3: make_shared for Single Allocation

**What:** Replace `new T(); shared_ptr<T>(p)` with `std::make_shared<T>()` to combine control block and object allocation.

**When to use:** Any shared_ptr construction where the object is immediately wrapped.

**Example (Bytes.cpp modification):**
```cpp
// Before:
void Bytes::newData(size_t capacity) {
    Data* data = new Data();
    // ...
    _data = SharedData(data);  // Two allocations
}

// After:
void Bytes::newData(size_t capacity) {
    _data = std::make_shared<Data>();  // Single allocation
    if (capacity > 0) {
        _data->reserve(capacity);
    }
    _exclusive = true;
}
```

### Pattern 4: Deferred Allocation for PacketReceipt

**What:** Default constructor creates a "null" PacketReceipt without allocating Object. Allocation happens on first use.

**When to use:** When most default-constructed objects are never used (receipts with NONE type).

**Example:**
```cpp
// Before (Packet.h:51-52):
PacketReceipt() : _object(new Object()) {}  // Always allocates

// After:
PacketReceipt() : _object(nullptr) {}  // Deferred - no allocation

// Methods that need valid _object call ensure_object():
void PacketReceipt::ensure_object() {
    if (!_object) {
        _object = std::make_shared<Object>();
    }
}
```

**API consideration:** Methods that access _object need null checks or ensure_object() calls. The `operator bool()` already returns false for null _object, so existing checks like `if (receipt)` work correctly.

### Pattern 5: BLE Cache with Insertion-Order Eviction

**What:** Replace std::map with a structure that tracks insertion order and protects connected devices.

**When to use:** The discovered devices cache in NimBLEPlatform.

**Implementation approach:**
```cpp
// Use std::map with a separate order tracking vector
// Key insight: std::map iterators are stable, but begin() is not insertion-order

// Option 1: Parallel tracking with std::vector for insertion order
std::vector<std::string> _discovered_order;  // Insertion order
std::map<std::string, NimBLEAdvertisedDevice> _discovered_devices;

// On insert: push to order vector, insert to map
// On eviction: iterate order vector, skip connected, erase first non-connected
// On connect: remove from order vector (device is now in _connections)

// Option 2: Use std::list for O(1) removal + std::map for lookup
// More complex but O(1) removal from middle
```

**Connected device protection:**
```cpp
bool isConnected(const std::string& addrKey) const {
    // Check if address is in _connections map
    for (const auto& [handle, conn] : _connections) {
        if (conn.address.toString() == addrKey) {
            return true;
        }
    }
    return false;
}
```

### Pattern 6: I2S Write with Timeout

**What:** Replace portMAX_DELAY with a reasonable timeout (1-2 seconds) for I2S writes.

**Example:**
```cpp
// Before (Tone.cpp:100):
i2s_write(I2S_PORT, samples, sizeof(samples), &bytes_written, portMAX_DELAY);

// After:
esp_err_t err = i2s_write(I2S_PORT, samples, sizeof(samples), &bytes_written,
                          pdMS_TO_TICKS(1000));  // 1 second timeout
if (err != ESP_OK || bytes_written == 0) {
    // Buffer full or I2S error - skip this sample batch
    WARNING("I2S write timeout or error, skipping samples");
}
```

### Anti-Patterns to Avoid

- **Blanket LVGL_LOCK everywhere:** Only add locks where lv_* calls actually occur. Over-locking increases contention.
- **Changing API semantics:** PacketReceipt deferred allocation should be transparent to callers. Existing `if (receipt)` checks must continue to work.
- **Complex eviction policies:** CONTEXT.md specifies insertion-order, not LRU. Keep it simple.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Shared pointer allocation | Two-step new + wrap | std::make_shared<T>() | Combines allocations, exception-safe |
| Mutex with timeout | Custom wrapper | xSemaphoreTakeRecursive with timeout | FreeRTOS built-in |
| JSON document | DynamicJsonDocument | JsonDocument | ArduinoJson v7 API |

**Key insight:** All solutions use existing APIs. No custom allocators or data structures needed.

## Common Pitfalls

### Pitfall 1: LVGL_LOCK in Event Callbacks

**What goes wrong:** Adding LVGL_LOCK inside event callback handlers causes recursive mutex acquisition (which works, but is unnecessary overhead).

**Why it happens:** Not understanding that event callbacks already run within lv_task_handler() which holds the mutex.

**How to avoid:** Event callbacks (on_*_clicked, on_*_changed) do NOT need LVGL_LOCK. Only constructors, destructors, and methods called from non-LVGL threads need it.

**Warning signs:** Excessive mutex acquisition warnings in logs; performance degradation.

### Pitfall 2: PacketReceipt Null Dereference

**What goes wrong:** After making default constructor defer allocation, methods that access _object without checking crash.

**Why it happens:** Not auditing all methods that access _object members.

**How to avoid:**
1. Grep for all `_object->` and `assert(_object)` in PacketReceipt
2. Add null check or ensure_object() call to each
3. Verify operator bool() returns false for null _object

**Warning signs:** Crashes when using PacketReceipt with no actual receipt.

### Pitfall 3: BLE Cache Eviction During Iteration

**What goes wrong:** Evicting from _discovered_devices while iterating causes iterator invalidation.

**Why it happens:** std::map::erase invalidates only the erased iterator, but if using a separate order vector, must handle both correctly.

**How to avoid:** Collect items to evict first, then erase in a separate pass. Or use iterator-safe removal patterns.

### Pitfall 4: ArduinoJson Elastic Allocation

**What goes wrong:** JsonDocument (v7) has no size limit by default, potentially using excessive memory.

**Why it happens:** v6's DynamicJsonDocument required explicit size; v7 grows automatically.

**How to avoid:** For embedded use, consider setting capacity limit or monitoring memory during serialization. The current codebase uses fixed DOCUMENT_MAXSIZE which may need adaptation.

## Code Examples

### Complete LVGL_LOCK Debug Timeout Implementation

```cpp
// Source: src/UI/LVGL/LVGLLock.h (full replacement)
class LVGLLock {
public:
    LVGLLock() {
        SemaphoreHandle_t mutex = LVGLInit::get_mutex();
        if (mutex) {
#if defined(DEBUG) || defined(LVGL_MUTEX_DEBUG)
            // Debug: 5s timeout with crash on deadlock
            BaseType_t result = xSemaphoreTakeRecursive(mutex, pdMS_TO_TICKS(5000));
            if (result != pdTRUE) {
                ERROR("LVGL mutex timeout (5s) - deadlock detected");
                // Log current task for debugging
                TaskHandle_t holder = xSemaphoreGetMutexHolder(mutex);
                if (holder) {
                    ERRORF("Mutex held by task: %s", pcTaskGetName(holder));
                }
                assert(false && "LVGL mutex deadlock");
            }
            _acquired = true;
#else
            // Release: infinite wait (production behavior)
            xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
            _acquired = true;
#endif
        }
    }
    // ... destructor unchanged ...
};
```

### Bytes make_shared Pattern

```cpp
// Source: src/Bytes.cpp (modification to newData)
void Bytes::newData(size_t capacity) {
    // Single allocation via make_shared (combines control block + Data)
    _data = std::make_shared<Data>();
    if (!_data) {
        ERROR("Bytes failed to allocate data buffer");
        throw std::runtime_error("Failed to allocate data buffer");
    }
    if (capacity > 0) {
        _data->reserve(capacity);
    }
    _exclusive = true;
}

// Similar change in exclusiveData() for the COW copy path
void Bytes::exclusiveData(bool copy, size_t capacity) {
    if (!_data) {
        newData(capacity);
    }
    else if (!_exclusive) {
        if (copy && !_data->empty()) {
            // COW copy with make_shared
            auto new_data = std::make_shared<Data>();
            size_t reserve_size = (capacity > _data->size()) ? capacity : _data->size();
            new_data->reserve(reserve_size);
            new_data->insert(new_data->begin(), _data->begin(), _data->end());
            _data = new_data;
            _exclusive = true;
        }
        else {
            newData(capacity);
        }
    }
    else if (capacity > 0 && capacity > size()) {
        reserve(capacity);
    }
}
```

### BLE Cache with Connected Device Protection

```cpp
// Source: NimBLEPlatform.h additions
private:
    // Insertion-order tracking for FIFO eviction
    std::vector<std::string> _discovered_order;

    // Check if a device address is currently connected
    bool isDeviceConnected(const std::string& addrKey) const;

// Source: NimBLEPlatform.cpp
bool NimBLEPlatform::isDeviceConnected(const std::string& addrKey) const {
    for (const auto& [handle, conn] : _connections) {
        // Compare MAC addresses
        if (conn.address.toString() == addrKey) {
            return true;
        }
    }
    return false;
}

void NimBLEPlatform::onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
    // ... existing service check ...

    std::string addrKey = advertisedDevice->getAddress().toString().c_str();

    // Eviction with connected device protection
    static constexpr size_t MAX_DISCOVERED_DEVICES = 16;
    while (_discovered_devices.size() >= MAX_DISCOVERED_DEVICES) {
        bool evicted = false;
        // Find oldest non-connected device
        for (auto it = _discovered_order.begin(); it != _discovered_order.end(); ++it) {
            if (!isDeviceConnected(*it)) {
                _discovered_devices.erase(*it);
                _discovered_order.erase(it);
                evicted = true;
                break;
            }
        }
        if (!evicted) {
            // All cached devices are connected - don't cache new one
            WARNING("Cannot cache device - all slots hold connected devices");
            return;
        }
    }

    // Add/update device
    auto existing = _discovered_devices.find(addrKey);
    if (existing == _discovered_devices.end()) {
        // New device - add to order tracking
        _discovered_order.push_back(addrKey);
    }
    _discovered_devices[addrKey] = *advertisedDevice;
}
```

### Connection Mutex Timeout Logging

```cpp
// Source: NimBLEPlatform.cpp modification
if (xSemaphoreTake(_conn_mutex, pdMS_TO_TICKS(100))) {
    auto cachedIt = _discovered_devices.find(addrKey);
    if (cachedIt != _discovered_devices.end()) {
        // Also remove from order tracking
        auto orderIt = std::find(_discovered_order.begin(),
                                  _discovered_order.end(), addrKey);
        if (orderIt != _discovered_order.end()) {
            _discovered_order.erase(orderIt);
        }
        _discovered_devices.erase(cachedIt);
    }
    xSemaphoreGive(_conn_mutex);
} else {
    // CONC-M5: Log timeout failures
    WARNING("NimBLEPlatform: conn_mutex timeout (100ms) during cache update");
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| DynamicJsonDocument | JsonDocument | ArduinoJson 7.0 | Size parameter removed, elastic allocation |
| new + shared_ptr wrap | std::make_shared | C++11 best practice | Single allocation, exception safety |
| portMAX_DELAY everywhere | Timeout in debug | Production readiness | Deadlock detection |

**Deprecated/outdated:**
- `DynamicJsonDocument` - Replaced by `JsonDocument` in ArduinoJson 7.x. The codebase has already migrated in Persistence.cpp, but the extern declaration pattern in Persistence.h needs verification.

## Open Questions

### Question 1: PacketReceipt Usage Analysis

**What we know:** Default constructor currently allocates Object immediately. Many PacketReceipts may be created with NONE type and never used.

**What's unclear:** Exact usage patterns - how often are default-constructed PacketReceipts actually accessed?

**Recommendation:** Proceed with deferred allocation but add defensive null checks to all _object accessor methods. The change is safe if properly guarded.

### Question 2: DEBUG Macro Definition

**What we know:** The codebase uses `NDEBUG` to control DEBUG/TRACE macros. Need to verify what macro controls debug builds.

**What's unclear:** Exact preprocessor symbol for debug vs release builds in platformio.ini.

**Recommendation:** Check platformio.ini for build_type or debug flags. May need to define `LVGL_MUTEX_DEBUG` explicitly or use existing debug detection.

## Sources

### Primary (HIGH confidence)
- `/freertos/freertos-kernel` via Context7 - Mutex API, xSemaphoreTakeRecursive, timeout behavior
- `/bblanchon/arduinojson` via Context7 - JsonDocument v7 API, migration from DynamicJsonDocument
- `/lvgl/lvgl` via Context7 - Thread safety patterns, lv_lock/lv_unlock

### Secondary (MEDIUM confidence)
- Codebase audit documents: 03-AUDIT.md, 04-LVGL.md, 04-NIMBLE.md, 04-MUTEX.md
- Existing code patterns in ChatScreen.cpp, Buffer.cpp, Persistence.cpp

### Tertiary (LOW confidence)
- None - all findings verified against codebase and official documentation

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - All libraries already in use
- Architecture patterns: HIGH - Patterns exist in codebase, just need extension
- Pitfalls: HIGH - Based on existing audit findings and code review
- Code examples: HIGH - Derived from existing codebase patterns

**Research date:** 2026-01-24
**Valid until:** Indefinite - patterns are stable, no API changes expected
