# NimBLE Platform Concurrency Audit

**Phase:** 04-concurrency-audit
**Requirement:** CONC-02 - Audit NimBLE Init/Deinit Lifecycle
**Date:** 2026-01-24

---

## Executive Summary

The NimBLE platform implementation in microReticulum demonstrates a **well-designed threading model** with proper synchronization mechanisms. The codebase uses a layered approach: spinlocks for fast state transitions, semaphores for longer operations, and deferred work queues to avoid blocking in callbacks.

**Issue Summary:**
- **Critical:** 0
- **High:** 2
- **Medium:** 3
- **Low:** 2
- **Total:** 7 issues identified

**Overall Assessment:** The BLE subsystem is thread-safe for normal operation. The main concerns are edge cases during shutdown/restart cycles and potential resource leaks in error recovery paths.

---

## Lifecycle Audit

### Initialization Sequence

```
                    Application Layer
                          |
                          v
    +------------------------------------------------------+
    |                BLEInterface::start()                  |
    |  - Validate identity                                  |
    |  - Create platform via factory                        |
    |  - Call platform->initialize()                        |
    +------------------------------------------------------+
                          |
                          v
    +------------------------------------------------------+
    |           NimBLEPlatform::initialize()               |
    |  1. Check _initialized guard                          |
    |  2. NimBLEDevice::init(device_name)                  |
    |  3. NimBLEDevice::setOwnAddrType(RANDOM)             |
    |  4. NimBLEDevice::setPower(ESP_PWR_LVL_P9)           |
    |  5. NimBLEDevice::setMTU(preferred_mtu)              |
    |  6. setupServer() [if PERIPHERAL/DUAL]               |
    |  7. setupScan() [if CENTRAL/DUAL]                    |
    |  8. Set _initialized = true                          |
    |  9. Set _gap_state = READY (spinlock protected)      |
    +------------------------------------------------------+
                          |
                          v
    +------------------------------------------------------+
    |           NimBLEPlatform::start()                    |
    |  - Check _initialized                                |
    |  - Check _running                                    |
    |  - startAdvertising() [if PERIPHERAL/DUAL]           |
    |  - Set _running = true                               |
    +------------------------------------------------------+
```

**Key Points:**
- Double-init protected by `_initialized` flag
- Server/scan setup order is well-defined
- Initial state is `GAPState::READY` after init

### Shutdown Sequence

```
    +------------------------------------------------------+
    |             NimBLEPlatform::shutdown()               |
    |  1. stop() -> stopScan(), stopAdvertising(),         |
    |               disconnectAll()                         |
    +------------------------------------------------------+
                          |
                          v
    +------------------------------------------------------+
    |              Client Cleanup Loop                      |
    |  for each client in _clients:                        |
    |    NimBLEDevice::deleteClient(client)                |
    |  _clients.clear()                                    |
    |  _connections.clear()                                |
    |  _discovered_devices.clear()                         |
    +------------------------------------------------------+
                          |
                          v
    +------------------------------------------------------+
    |           NimBLEDevice::deinit(true)                 |
    |  - true = clear all internal state                   |
    |  - Releases BLE controller resources                 |
    +------------------------------------------------------+
                          |
                          v
    +------------------------------------------------------+
    |              Clear Object Pointers                    |
    |  _server = nullptr                                   |
    |  _service = nullptr                                  |
    |  _rx_char = nullptr                                  |
    |  _tx_char = nullptr                                  |
    |  _identity_char = nullptr                            |
    |  _scan = nullptr                                     |
    |  _advertising_obj = nullptr                          |
    +------------------------------------------------------+
```

### Double-Init Protection Analysis

**Implementation (NimBLEPlatform.cpp:94-98):**
```cpp
if (_initialized) {
    WARNING("NimBLEPlatform: Already initialized");
    return true;
}
```

**Assessment:**
- Guard works correctly for sequential calls
- **Not thread-safe** for concurrent initialization attempts (no mutex)
- In practice, `initialize()` is only called from `BLEInterface::start()` which itself is only called from the main task, so concurrent init is unlikely

### Restart Cycle Concerns

The error recovery mechanism in `recoverBLEStack()` has potential issues:

**Pattern (NimBLEPlatform.cpp:244-290):**
```cpp
bool NimBLEPlatform::recoverBLEStack() {
    _lightweight_reset_fails++;

    if (_lightweight_reset_fails >= 5) {
        ESP.restart();  // Full device restart
        return false;
    }

    enterErrorRecovery();
    // ... soft reset operations
}
```

**Concerns:**
1. Soft reset does NOT call `NimBLEDevice::deinit()` - internal state may leak
2. After 5 failures, device restarts - no graceful cleanup
3. No tracking of how many restart cycles have occurred across boot

---

## Callback Threading Model

All NimBLE callbacks execute in the **NimBLE host task** (managed by ESP-IDF/NimBLE, not app code). This has important threading implications.

### Callback Inventory

| Callback | Class | Context | Blocks? | Defers Work? | Thread-Safe? |
|----------|-------|---------|---------|--------------|--------------|
| `onConnect(NimBLEServer*)` | NimBLEServerCallbacks | NimBLE host | No | No | Yes |
| `onDisconnect(NimBLEServer*)` | NimBLEServerCallbacks | NimBLE host | No | No | Yes |
| `onMTUChange()` | NimBLEServerCallbacks | NimBLE host | No | No | Yes |
| `onWrite()` | NimBLECharacteristicCallbacks | NimBLE host | No | Yes | Yes |
| `onRead()` | NimBLECharacteristicCallbacks | NimBLE host | No | No | Yes |
| `onSubscribe()` | NimBLECharacteristicCallbacks | NimBLE host | No | No | Yes |
| `onConnect(NimBLEClient*)` | NimBLEClientCallbacks | NimBLE host | No | No | Yes |
| `onConnectFail()` | NimBLEClientCallbacks | NimBLE host | No | No | Yes |
| `onDisconnect(NimBLEClient*)` | NimBLEClientCallbacks | NimBLE host | No | No | Yes |
| `onResult()` | NimBLEScanCallbacks | NimBLE host | No | No | Yes |
| `onScanEnd()` | NimBLEScanCallbacks | NimBLE host | No | No | Yes |
| `nativeGapEventHandler()` | Static function | NimBLE host | No | No | Yes |

### Deferred Work Pattern

The BLEInterface uses a deferred work pattern to avoid stack overflow and blocking in NimBLE callbacks.

**Queuing Pattern (BLEInterface.cpp:641-654):**
```cpp
void BLEInterface::onHandshakeComplete(const Bytes& mac,
                                        const Bytes& identity,
                                        bool is_central) {
    // Queue for processing in loop()
    if (_pending_handshakes.size() >= MAX_PENDING_HANDSHAKES) {
        WARNING("Pending handshake queue full, dropping");
        return;
    }
    PendingHandshake pending;
    pending.mac = mac;
    pending.identity = identity;
    pending.is_central = is_central;
    _pending_handshakes.push_back(pending);
}
```

**Processing Pattern (BLEInterface.cpp:128-152):**
```cpp
void BLEInterface::loop() {
    // Process pending handshakes (deferred from callback)
    if (!_pending_handshakes.empty()) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        for (const auto& pending : _pending_handshakes) {
            // ... process handshake
        }
        _pending_handshakes.clear();
    }

    // Process pending data fragments
    if (!_pending_data.empty()) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        for (const auto& pending : _pending_data) {
            _reassembler.processFragment(pending.identity, pending.data);
        }
        _pending_data.clear();
    }
}
```

### Synchronization Mechanisms Used

1. **Spinlock (`_state_mux`)** - Fast state transitions
2. **Semaphore (`_conn_mutex`)** - Connection map access with 100ms timeout
3. **Recursive Mutex (`_mutex` in BLEInterface)** - Callback data protection

---

## State Machine Documentation

### GAPState Enum

```cpp
enum class GAPState : uint8_t {
    UNINITIALIZED,   // BLE not started
    INITIALIZING,    // NimBLE init in progress
    READY,           // Idle, ready for operations
    MASTER_PRIORITY, // Master operation in progress, slave paused
    SLAVE_PRIORITY,  // Slave operation in progress, master paused
    TRANSITIONING,   // State change in progress
    ERROR_RECOVERY   // Recovering from error
};
```

### MasterState Enum (Central Role)

```cpp
enum class MasterState : uint8_t {
    IDLE,           // No master operations
    SCAN_STARTING,  // Gap scan start requested
    SCANNING,       // Actively scanning
    SCAN_STOPPING,  // Gap scan stop requested
    CONN_STARTING,  // Connection initiation requested
    CONNECTING,     // Connection in progress
    CONN_CANCELING  // Connection cancel requested
};
```

### SlaveState Enum (Peripheral Role)

```cpp
enum class SlaveState : uint8_t {
    IDLE,           // Not advertising
    ADV_STARTING,   // Gap adv start requested
    ADVERTISING,    // Actively advertising
    ADV_STOPPING    // Gap adv stop requested
};
```

### State Transition Diagram

```
                         +----------------+
                         | UNINITIALIZED  |
                         +-------+--------+
                                 |
                         initialize()
                                 |
                                 v
+--------+  error   +----------------+
| ERROR  |<---------+     READY      |<--------------+
|RECOVERY|          +-------+--------+               |
+---+----+                  |                        |
    |                       |                        |
    +--------->-------------+                        |
      recovery             |                         |
      complete        startScan()/                   |
                     startAdvertising()              |
                           |                         |
          +----------------+----------------+        |
          |                                 |        |
          v                                 v        |
+------------------+               +------------------+
| MASTER_PRIORITY  |               | SLAVE_PRIORITY   |
| (scan/connect)   |               | (advertising)    |
+------------------+               +------------------+
          |                                 |
    scan complete/                    advertising
    connect done                      stopped
          |                                 |
          +----------------+----------------+
                           |
                           v
                    +------+------+
                    |    READY    |
                    +-------------+
```

### Spinlock Protection Pattern

**Atomic State Transition (NimBLEPlatform.cpp:296-309):**
```cpp
bool NimBLEPlatform::transitionMasterState(MasterState expected,
                                           MasterState new_state) {
    bool ok = false;
    portENTER_CRITICAL(&_state_mux);
    if (_master_state == expected) {
        _master_state = new_state;
        ok = true;
    }
    portEXIT_CRITICAL(&_state_mux);
    return ok;
}
```

**State Check Pattern (NimBLEPlatform.cpp:341-350):**
```cpp
bool NimBLEPlatform::canStartScan() const {
    bool ok = false;
    portENTER_CRITICAL(&_state_mux);
    ok = (_gap_state == GAPState::READY ||
          _gap_state == GAPState::MASTER_PRIORITY)
         && _master_state == MasterState::IDLE
         && !ble_gap_disc_active()
         && !ble_gap_conn_active();
    portEXIT_CRITICAL(&_state_mux);
    return ok;
}
```

### Race Condition Analysis

**Potential Race: Pause/Resume Slave**

The `pauseSlaveForMaster()` and `resumeSlave()` functions coordinate advertising with scan/connect operations. A race could occur if:

1. Thread A calls `pauseSlaveForMaster()` - sets `_slave_paused_for_master = true`
2. Thread B (callback) calls `resumeSlave()` - clears flag
3. Thread A's operation completes, calls `resumeSlave()` - already cleared, no resume

**Mitigation:** The flag is now checked atomically (NimBLEPlatform.cpp:443-451):
```cpp
void NimBLEPlatform::resumeSlave() {
    bool should_resume = false;
    portENTER_CRITICAL(&_state_mux);
    if (_slave_paused_for_master) {
        _slave_paused_for_master = false;
        should_resume = true;
    }
    portEXIT_CRITICAL(&_state_mux);
    // ...
}
```

---

## Findings

### NIMBLE-01: Pending Queues Not Thread-Safe [HIGH]

**Location:** `BLEInterface.cpp` lines 128-161, 641-653, 859-875

**Description:** The `_pending_handshakes` and `_pending_data` vectors are modified in NimBLE callbacks (push_back) and read/cleared in the BLE task loop. While the loop() holds `_mutex` during processing, the callbacks that push to these vectors do NOT hold the mutex.

**Code Snippet:**
```cpp
// In callback (NO MUTEX)
_pending_handshakes.push_back(pending);

// In loop() (WITH MUTEX)
std::lock_guard<std::recursive_mutex> lock(_mutex);
for (const auto& pending : _pending_handshakes) { ... }
_pending_handshakes.clear();
```

**Impact:** If a callback adds to the vector while loop() is iterating or clearing, data corruption or crash could occur.

**Recommended Fix:**
```cpp
void BLEInterface::onHandshakeComplete(...) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);  // ADD LOCK
    if (_pending_handshakes.size() >= MAX_PENDING_HANDSHAKES) {
        return;
    }
    _pending_handshakes.push_back(pending);
}
```

---

### NIMBLE-02: Shutdown During Active Operations [HIGH]

**Location:** `NimBLEPlatform::shutdown()` (NimBLEPlatform.cpp:206-234)

**Description:** The shutdown sequence does not wait for pending operations to complete. If a connection is in progress during shutdown, callbacks may fire after objects are cleared.

**Code Snippet:**
```cpp
void NimBLEPlatform::shutdown() {
    stop();  // stopScan(), stopAdvertising(), disconnectAll()

    // Immediately clear everything - callbacks may still be pending!
    for (auto& kv : _clients) {
        NimBLEDevice::deleteClient(kv.second);
    }
    _clients.clear();
    // ...
    NimBLEDevice::deinit(true);
}
```

**Impact:** Use-after-free if callback tries to access deleted objects.

**Recommended Fix:**
1. Add a "shutting down" state to prevent new operations
2. Wait for pending operations to complete (with timeout)
3. Disable callbacks before clearing containers
4. Add nullptr checks in all callbacks

---

### NIMBLE-03: Soft Reset Does Not Release NimBLE Internal State [MEDIUM]

**Location:** `NimBLEPlatform::recoverBLEStack()` (NimBLEPlatform.cpp:244-290)

**Description:** The soft reset (`enterErrorRecovery()`) does not call `NimBLEDevice::deinit()`. If the BLE stack is in a bad state, internal NimBLE resources may not be released.

**Code Snippet:**
```cpp
bool NimBLEPlatform::recoverBLEStack() {
    enterErrorRecovery();

    // Reconfigure scan (does NOT release existing resources)
    if (_scan) {
        _scan->setScanCallbacks(this, false);
        _scan->clearResults();
    }
    // No NimBLEDevice::deinit() called
}
```

**Impact:** Memory leak after multiple recovery cycles; stack may eventually enter unrecoverable state.

**Recommended Fix:** Consider a "hard" recovery option that does full deinit/init cycle:
```cpp
if (_lightweight_reset_fails >= 3) {
    NimBLEDevice::deinit(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    NimBLEDevice::init(_config.device_name);
    // Reconfigure server/scan
}
```

---

### NIMBLE-04: Connection Mutex Timeout May Lose Updates [MEDIUM]

**Location:** `NimBLEPlatform::connect()` (NimBLEPlatform.cpp:837-844)

**Description:** Connection map access uses a 100ms timeout. If the timeout expires, the discovered devices cache update is skipped.

**Code Snippet:**
```cpp
if (xSemaphoreTake(_conn_mutex, pdMS_TO_TICKS(100))) {
    auto cachedIt = _discovered_devices.find(addrKey);
    if (cachedIt != _discovered_devices.end()) {
        _discovered_devices.erase(cachedIt);
    }
    xSemaphoreGive(_conn_mutex);
}
// No else clause - silently fails
```

**Impact:** Stale entries in discovered devices cache; potential memory growth.

**Recommended Fix:** Add logging for failed mutex acquisition:
```cpp
} else {
    WARNING("Failed to acquire conn_mutex for cache update");
}
```

---

### NIMBLE-05: Discovered Devices Cache Unbounded Growth [MEDIUM]

**Location:** `NimBLEPlatform::onResult()` (NimBLEPlatform.cpp:1736-1762)

**Description:** While there is a MAX_DISCOVERED_DEVICES limit (16), the eviction is FIFO-based without considering connection status. Connected devices should not be evicted.

**Code Snippet:**
```cpp
static constexpr size_t MAX_DISCOVERED_DEVICES = 16;
while (_discovered_devices.size() >= MAX_DISCOVERED_DEVICES) {
    auto oldest = _discovered_devices.begin();
    _discovered_devices.erase(oldest);  // May evict connected device
}
```

**Impact:** Device info for active connections could be lost.

**Recommended Fix:** Check if device is connected before eviction.

---

### NIMBLE-06: Native GAP Event Handler Uses Volatile for Complex State [LOW]

**Location:** `NimBLEPlatform.h:287-291`, `nativeGapEventHandler()` (NimBLEPlatform.cpp:857-948)

**Description:** Multiple volatile variables are used to communicate between the GAP event handler and the connect function. Volatile does not guarantee atomicity for multi-variable state.

**Code Snippet:**
```cpp
volatile bool _native_connect_pending = false;
volatile bool _native_connect_success = false;
volatile int _native_connect_result = 0;
volatile uint16_t _native_connect_handle = 0;
```

**Impact:** Theoretical race if compiler reorders accesses, though unlikely in practice.

**Recommended Fix:** Use a single atomic struct or add a mutex for the connection state.

---

### NIMBLE-07: Undocumented 50ms Delay in Error Recovery [LOW]

**Location:** `NimBLEPlatform::enterErrorRecovery()` (NimBLEPlatform.cpp:510-511)

**Description:** There's a magic 50ms delay with a comment about ESP32-S3 settling time, but no documentation of why this value was chosen.

**Code Snippet:**
```cpp
// ESP32-S3 settling time after sync (reduced from 200ms)
delay(50);
```

**Impact:** May be insufficient or excessive; unclear if it's platform-specific.

**Recommended Fix:** Add documentation with rationale, consider making configurable.

---

## BLE Task Analysis

### Task Configuration

| Property | Value | Notes |
|----------|-------|-------|
| Stack Size | 8192 bytes (8KB) | Adequate for most operations |
| Priority | Configurable (default 1) | Below LVGL task |
| Core | 0 (configurable) | Same as BT controller |
| Yield Delay | 10ms | `vTaskDelay(pdMS_TO_TICKS(10))` |

### Stack Usage Assessment

The 8KB stack is reasonable for BLE operations. Key stack consumers:
- GATT operations (service discovery, read/write)
- Callback chains (up to 3-4 levels deep)
- Local buffers in fragmentation/reassembly

**Risk:** Crypto operations in callbacks could add significant stack pressure. The Phase 1 memory monitor with 256-byte warning threshold should catch issues early.

### Yield Pattern

```cpp
void BLEInterface::ble_task(void* param) {
    while (true) {
        self->loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

The 10ms yield is appropriate for BLE:
- Short enough for responsive connections
- Long enough to avoid CPU starvation
- Matches typical BLE connection interval

### Core Affinity Implications

Running on core 0 (same as BT controller) has tradeoffs:
- **Pro:** Reduces context switch overhead for BLE operations
- **Pro:** Better cache locality with controller
- **Con:** Shares CPU with WiFi driver (on ESP32)
- **Con:** May compete with other system tasks

---

## Cross-Reference

This audit addresses **CONC-02** from the phase requirements:
- [x] NimBLE init/deinit lifecycle documented
- [x] All GAP/GATT callbacks inventoried
- [x] State machine documented with race condition analysis
- [x] BLE task integration documented

---

## Summary

The NimBLE platform implementation is well-designed with proper synchronization for normal operation. The main areas for improvement are:

1. **Thread-safe pending queues** (NIMBLE-01) - High priority fix
2. **Graceful shutdown handling** (NIMBLE-02) - High priority fix
3. **Error recovery resource management** (NIMBLE-03) - Medium priority

The state machine design with atomic transitions is solid, and the deferred work pattern correctly isolates callback processing from BLE stack constraints.
