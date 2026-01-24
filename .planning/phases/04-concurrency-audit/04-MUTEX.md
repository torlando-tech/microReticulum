# Mutex Ordering and Deadlock Analysis

**Phase:** 04-concurrency-audit
**Plan:** 04-04
**Date:** 2026-01-24
**Requirement:** CONC-04

---

## Executive Summary

The microReticulum codebase uses **5 distinct synchronization primitives** across 3 subsystems. Analysis reveals a **well-layered architecture** with minimal cross-subsystem mutex acquisition, resulting in **low deadlock risk** under current design.

| Metric | Value |
|--------|-------|
| Total Mutexes | 5 |
| Spinlocks | 1 (50+ usage sites) |
| Cross-layer Acquisitions | 0 detected |
| Deadlock Risk | **Low** |
| Issues Found | 1 Medium, 1 Low |

**Key Finding:** The current codebase follows an implicit layered mutex ordering without formal enforcement. While no actual deadlock paths were identified, the use of `portMAX_DELAY` in multiple locations means that if a deadlock were to occur, it would be unrecoverable without device reset.

---

## Mutex Inventory

| Name | Type | Location | Purpose | Timeout |
|------|------|----------|---------|---------|
| `LVGLInit::_mutex` | Recursive Semaphore | `src/UI/LVGL/LVGLInit.cpp:38` | LVGL API thread-safety | `portMAX_DELAY` (infinite) |
| `BLEInterface::_mutex` | `std::recursive_mutex` | `examples/common/ble_interface/BLEInterface.h:272` | Callback data protection | None (blocks) |
| `NimBLEPlatform::_conn_mutex` | Binary Semaphore | `src/BLE/platforms/NimBLEPlatform.cpp:79` | Connection map access | 100ms |
| `NimBLEPlatform::_state_mux` | `portMUX_TYPE` (spinlock) | `src/BLE/platforms/NimBLEPlatform.h:213` | State machine transitions | N/A (spinlock) |
| `SX1262Interface::_spi_mutex` | Binary Semaphore | `examples/lxmf_tdeck/lib/sx1262_interface/SX1262Interface.cpp:62` | SPI bus access | 100-1000ms |

### Mutex Details

**1. LVGL Mutex (`LVGLInit::_mutex`)**
- **Type:** Recursive (`xSemaphoreCreateRecursiveMutex`)
- **Purpose:** Protects all LVGL API calls; required because LVGL is not thread-safe
- **Holders:**
  - LVGL task (during `lv_task_handler()`)
  - Main task (via `LVGL_LOCK()` macro in UIManager and screens)
- **Timeout:** `portMAX_DELAY` (infinite wait)
- **Usage Sites:** 64 `LVGL_LOCK()` calls across 10 UI files
- **Risk:** Infinite wait could mask deadlock

**2. BLE Interface Mutex (`BLEInterface::_mutex`)**
- **Type:** `std::recursive_mutex` (C++ standard library)
- **Purpose:** Protects callback data (`_pending_handshakes`, `_pending_data`, connection tracking)
- **Holders:**
  - BLE task (during `loop()` processing)
  - NimBLE callbacks (via `std::lock_guard`)
- **Usage Sites:** 18 `std::lock_guard` acquisitions in BLEInterface.cpp
- **Risk:** `std::recursive_mutex` blocks indefinitely

**3. NimBLE Connection Mutex (`NimBLEPlatform::_conn_mutex`)**
- **Type:** Binary semaphore (`xSemaphoreCreateMutex`)
- **Purpose:** Protects `_connections`, `_clients`, `_discovered_devices` maps
- **Timeout:** 100ms (`pdMS_TO_TICKS(100)`)
- **Usage Sites:** 1 acquisition site (connect function)
- **Risk:** Low - has timeout, but failure silently skips cache update

**4. NimBLE State Spinlock (`NimBLEPlatform::_state_mux`)**
- **Type:** `portMUX_TYPE` (ESP-IDF spinlock)
- **Purpose:** Atomic state machine transitions for GAP/master/slave states
- **Usage Sites:** 50+ `portENTER_CRITICAL`/`portEXIT_CRITICAL` pairs
- **Characteristics:**
  - Disables interrupts on current core
  - Very short hold times (state check/update only)
  - Non-blocking (spinlock)
- **Risk:** Very low - designed for atomic operations only

**5. SPI Mutex (`SX1262Interface::_spi_mutex`)**
- **Type:** Binary semaphore
- **Purpose:** Protects HSPI bus shared between LoRa radio and display
- **Timeout:** Variable (100ms for reads, 1000ms for init/transmit)
- **Usage Sites:** 5 acquisition sites in SX1262Interface.cpp
- **Risk:** Low - has timeout with error handling

---

## Acquisition Analysis

### Code Path 1: LVGL Rendering (Normal Operation)

```
LVGL Task (Core 1)
    |
    +-> xSemaphoreTakeRecursive(_mutex, portMAX_DELAY)  [LVGL mutex]
    |       |
    |       +-> lv_task_handler()
    |               |
    |               +-> Event callbacks (already hold mutex)
    |
    +-> xSemaphoreGiveRecursive(_mutex)
```

**Mutexes acquired:** 1 (LVGL only)
**Risk:** None - single mutex path

### Code Path 2: UI Update from Main Task

```
Main Task (UIManager::update())
    |
    +-> LVGL_LOCK()  [LVGL mutex]
    |       |
    |       +-> Screen updates (lv_* calls)
    |
    +-> ~LVGLLock()  [LVGL mutex released]
```

**Mutexes acquired:** 1 (LVGL only)
**Risk:** None - single mutex path

### Code Path 3: BLE Message Delivery to UI

```
NimBLE Callback (onWrite)
    |
    +-> Queue data to _pending_data (NO LOCK currently - ISSUE NIMBLE-01)
    |
    v
BLE Task (loop())
    |
    +-> std::lock_guard<std::recursive_mutex>(_mutex)  [BLE mutex]
    |       |
    |       +-> Process fragments
    |       +-> Call onDataReceived callback
    |               |
    |               +-> LXMRouter::on_data()
    |                       |
    |                       +-> Queue message for UI
    |
    +-> ~lock_guard  [BLE mutex released]
    |
    v
Main Task (UIManager::update())
    |
    +-> LVGL_LOCK()  [LVGL mutex]
    |       |
    |       +-> Display received message
    |
    +-> ~LVGLLock()  [LVGL mutex released]
```

**Mutexes acquired:** 2 total, but NEVER nested:
- BLE mutex held in BLE task, released before UI update
- LVGL mutex held in Main task, acquired fresh

**Risk:** None - mutexes not nested

### Code Path 4: LoRa Transmission

```
Main Task
    |
    +-> xSemaphoreTake(_spi_mutex, 1000ms)  [SPI mutex]
    |       |
    |       +-> SPI transaction to SX1262
    |
    +-> xSemaphoreGive(_spi_mutex)
```

**Mutexes acquired:** 1 (SPI only)
**Risk:** Low - has timeout, isolated subsystem

### Code Path 5: BLE State Transition

```
Any Task
    |
    +-> portENTER_CRITICAL(&_state_mux)  [spinlock]
    |       |
    |       +-> Read/modify state (MasterState, SlaveState, GAPState)
    |       |   (< 10 CPU cycles)
    |
    +-> portEXIT_CRITICAL(&_state_mux)
```

**Mutexes acquired:** 1 spinlock (very short hold)
**Risk:** None - spinlocks are designed for this

---

## Deadlock Potential Assessment

### Assessment Methodology

1. Identified all mutex acquisition sites via grep
2. Traced code paths for each major operation
3. Checked for nested mutex acquisitions
4. Verified mutex ordering consistency

### Finding MUTEX-01: No Formal Ordering Enforcement [MEDIUM]

**Severity:** Medium
**Risk:** Architectural debt, not immediate threat

**Description:**
The codebase relies on implicit layering (UI calls don't reach into BLE internals, BLE doesn't update UI directly) but has no explicit ordering enforcement. If future changes introduce cross-subsystem calls, deadlock could occur.

**Current Mitigations:**
- UI and BLE subsystems communicate via message queues, not direct calls
- LVGL callbacks never acquire BLE mutexes
- BLE callbacks never acquire LVGL mutex
- Deferred work pattern breaks callback -> UI chain

**Recommended Action:**
Document the implicit ordering (see below) and add static analysis checks for violations.

### Finding MUTEX-02: portMAX_DELAY Masks Deadlock Detection [LOW]

**Severity:** Low (informational)

**Description:**
Both LVGL mutex and BLE mutex use indefinite waits:
- `xSemaphoreTakeRecursive(_mutex, portMAX_DELAY)` in LVGLInit.cpp:157
- `std::recursive_mutex` blocks indefinitely by default

If a deadlock were to occur, the system would hang forever with no diagnostic output.

**Recommended Action:**
Consider adding timeout-based acquisition with logging for debug builds:
```cpp
// Debug version with deadlock detection
if (xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    ERROR("LVGL mutex acquisition timeout - possible deadlock");
    // Consider TWDT trigger or stack dump
}
```

### Scenarios Analyzed (No Issues Found)

| Scenario | Analysis | Result |
|----------|----------|--------|
| LVGL task vs Main task | Both acquire same mutex (recursive allows reentry) | Safe |
| BLE callback -> UI update | Deferred via queue, no nested lock | Safe |
| UI -> BLE send | Calls into Interface abstraction, no direct mutex | Safe |
| LoRa TX during display update | Different mutex (SPI vs LVGL) | Safe |
| NimBLE state change during scan | Spinlock only, very short hold | Safe |
| Connection map access timeout | Has 100ms timeout with silent failure | Safe (data loss) |

---

## Recommended Acquisition Order

Based on system layering and current usage patterns, the recommended mutex acquisition order is:

```
  +------------------------------------------------------------------+
  |  Level 1 (Outermost - Acquire First)                              |
  |  LVGL Mutex (UI Layer)                                           |
  |  - All UI operations must acquire this first                     |
  |  - Held by: LVGL task, Main task (screen updates)                |
  +------------------------------------------------------------------+
                                |
                                v
  +------------------------------------------------------------------+
  |  Level 2 (Application Layer)                                      |
  |  BLE Interface Mutex (Network/Application boundary)              |
  |  - Protects application-level BLE state                          |
  |  - Held by: BLE task, NimBLE callbacks                           |
  +------------------------------------------------------------------+
                                |
                                v
  +------------------------------------------------------------------+
  |  Level 3 (Platform Layer)                                         |
  |  NimBLE Connection Mutex (Platform internals)                    |
  |  - Protects internal connection tracking                         |
  |  - Should only be acquired by NimBLE platform code               |
  +------------------------------------------------------------------+
                                |
                                v
  +------------------------------------------------------------------+
  |  Level 4 (Atomic Operations - Innermost)                          |
  |  State Spinlocks / SPI Mutex (Hardware/atomic)                   |
  |  - Very short hold times only                                    |
  |  - Never acquire higher-level mutexes while holding              |
  +------------------------------------------------------------------+
```

### Rationale

1. **LVGL outermost:** UI is the user-facing layer. All user-initiated operations start here. Acquiring LVGL first prevents UI freeze scenarios.

2. **BLE interface second:** Bridges user actions to network operations. Data flows UI -> Interface -> Platform.

3. **NimBLE platform third:** Internal implementation detail. Should never need to acquire UI or interface mutexes.

4. **Spinlocks innermost:** Designed for atomic operations only. Very short hold times ensure no blocking.

### Enforcement Strategy

Since C++ doesn't have built-in mutex ordering enforcement, use these techniques:

1. **Code review checklist:**
   - Any new cross-subsystem call? Check mutex ordering
   - Adding mutex acquisition? Verify ordering document

2. **Debug assertions (optional):**
   ```cpp
   #ifdef DEBUG_MUTEX_ORDER
   thread_local uint8_t _held_mutex_level = 0;

   class OrderedLock {
       uint8_t _level;
   public:
       OrderedLock(Mutex& m, uint8_t level) : _level(level) {
           assert(level > _held_mutex_level);
           m.lock();
           _held_mutex_level = level;
       }
       ~OrderedLock() { _held_mutex_level = _level - 1; }
   };
   #endif
   ```

3. **Static analysis (future):**
   - Clang thread safety annotations
   - Custom linter for mutex order violations

---

## Architecture Diagram: Mutex Dependency Graph

```
                        TASK ARCHITECTURE
    +------------------+  +------------------+  +------------------+
    |   LVGL Task      |  |    BLE Task      |  |   Main Task      |
    |   (Core 1)       |  |    (Core 0)      |  |   (Any Core)     |
    +--------+---------+  +--------+---------+  +--------+---------+
             |                     |                     |
             |                     |                     |
    +--------v---------+  +--------v---------+  +--------v---------+
    |  LVGL Mutex      |  |  BLE Mutex       |  |  LVGL Mutex      |
    |  (recursive)     |  |  (recursive)     |  |  (shared w/LVGL) |
    +------------------+  +--------+---------+  +------------------+
                                   |
                          +--------v---------+
                          |  Conn Mutex      |
                          |  (100ms timeout) |
                          +--------+---------+
                                   |
                          +--------v---------+
                          |  State Spinlock  |
                          |  (atomic only)   |
                          +------------------+


                       DATA FLOW (No Mutex Nesting)

    +----------------+      +----------------+      +----------------+
    | UI Event       |      | Message Queue  |      | BLE Callback   |
    | (LVGL mutex)   | ---> | (no lock)      | <--- | (BLE mutex)    |
    +----------------+      +----------------+      +----------------+
            |                                               |
            |    SAFE: Mutexes never held simultaneously    |
            |                                               |
            v                                               v
    +----------------+                              +----------------+
    | Screen Update  |                              | Queue to       |
    | (LVGL mutex)   |                              | pending_data   |
    +----------------+                              +----------------+


                    DANGER ZONES (NOT PRESENT IN CODEBASE)

    +-----------------+     X     +-----------------+
    | LVGL Callback   | ----X---> | BLE Send        |
    | (holding LVGL)  |     X     | (would need BLE)|
    +-----------------+     X     +-----------------+
                            X
                            X  <-- This path does NOT exist
                            X      (would cause ordering violation)

    +-----------------+     X     +-----------------+
    | BLE Callback    | ----X---> | UI Update       |
    | (holding BLE)   |     X     | (would need LVGL)|
    +-----------------+     X     +-----------------+
                            X
                            X  <-- This path does NOT exist
                            X      (deferred via queue instead)
```

---

## Cross-Reference

This audit addresses **CONC-04: Document mutex acquisition ordering** from the Phase 4 requirements.

**Related Documents:**
- `04-LVGL.md` - LVGL mutex usage patterns
- `04-NIMBLE.md` - NimBLE synchronization mechanisms
- `04-TASKS.md` - Task architecture and blocking operations

**Phase 5 Backlog Items:**
- P3-6: Consider adding timeout to LVGL mutex with deadlock logging
- P4-5: Add debug-mode mutex ordering assertions

---

## Summary

The microReticulum mutex architecture is **well-designed for deadlock prevention**:

1. **No cross-subsystem mutex nesting** - UI and BLE use message queues, not direct calls
2. **Proper use of recursive mutexes** - Handles LVGL's callback patterns correctly
3. **Spinlocks for atomic operations** - 50+ state transitions use appropriate primitives
4. **Timeouts where appropriate** - Connection and SPI mutexes have bounded waits

**Areas for Improvement:**
- Document the implicit ordering (DONE in this report)
- Consider debug-mode timeout with logging for LVGL mutex
- Add static analysis for cross-subsystem calls in code review
