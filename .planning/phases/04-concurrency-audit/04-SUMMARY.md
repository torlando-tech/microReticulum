# Phase 4: Concurrency Audit - Consolidated Summary

**Phase:** 04-concurrency-audit
**Date:** 2026-01-24
**Status:** Complete
**Requirements:** CONC-01 through CONC-05

---

## Executive Summary

Phase 4 audited concurrency patterns across LVGL UI, NimBLE BLE, and FreeRTOS task subsystems. The audit identified **18 issues** across 4 audit reports, with a well-designed threading model that minimizes deadlock risk through implicit layering.

| Severity | Count | Category |
|----------|-------|----------|
| Critical | 0 | - |
| High | 4 | TWDT config, CPU hogging, thread-safety, shutdown |
| Medium | 9 | Mutex waits, soft reset, cache growth, missing locks |
| Low | 5 | Volatile usage, delays, incomplete watchdog |
| **Total** | **18** | |

**Key Risks Identified:**
1. Task Watchdog Timer (TWDT) not configured for application tasks
2. Pending queues in BLEInterface lack thread-safe access
3. LXStamper CPU hogging during stamp generation
4. No formal mutex ordering enforcement

**Positive Findings:**
- RAII lock patterns correctly implemented
- Message queue decoupling prevents UI/BLE deadlocks
- 50+ spinlock sites use correct atomic patterns
- Recursive mutexes handle nested LVGL callbacks correctly

---

## Issue Summary

| ID | Severity | Subsystem | Issue | Source |
|----|----------|-----------|-------|--------|
| LVGL-01 | Medium | UI | SettingsScreen constructor/destructor missing LVGL_LOCK | 04-LVGL.md |
| LVGL-02 | Medium | UI | ComposeScreen constructor/destructor missing LVGL_LOCK | 04-LVGL.md |
| LVGL-03 | Medium | UI | AnnounceListScreen constructor/destructor missing LVGL_LOCK | 04-LVGL.md |
| NIMBLE-01 | High | BLE | Pending queues not thread-safe (push without lock) | 04-NIMBLE.md |
| NIMBLE-02 | High | BLE | Shutdown during active operations (use-after-free risk) | 04-NIMBLE.md |
| NIMBLE-03 | Medium | BLE | Soft reset does not release NimBLE internal state | 04-NIMBLE.md |
| NIMBLE-04 | Medium | BLE | Connection mutex timeout may lose cache updates | 04-NIMBLE.md |
| NIMBLE-05 | Medium | BLE | Discovered devices cache unbounded (may evict connected) | 04-NIMBLE.md |
| NIMBLE-06 | Low | BLE | Native GAP handler uses volatile for complex state | 04-NIMBLE.md |
| NIMBLE-07 | Low | BLE | Undocumented 50ms delay in error recovery | 04-NIMBLE.md |
| TASK-01 | High | FreeRTOS | TWDT not configured for application tasks | 04-TASKS.md |
| TASK-02 | High | FreeRTOS | LXStamper CPU hogging (yields every 100 rounds) | 04-TASKS.md |
| TASK-03 | Medium | FreeRTOS | LVGL mutex uses portMAX_DELAY (infinite wait) | 04-TASKS.md |
| TASK-04 | Medium | FreeRTOS | Audio I2S blocking write with portMAX_DELAY | 04-TASKS.md |
| TASK-05 | Low | FreeRTOS | Link watchdog TODO not implemented | 04-TASKS.md |
| MUTEX-01 | Medium | Cross-system | No formal mutex ordering enforcement | 04-MUTEX.md |
| MUTEX-02 | Low | Cross-system | portMAX_DELAY masks deadlock detection | 04-MUTEX.md |

---

## Severity Breakdown

### Critical Issues (0)

None identified. The threading architecture is sound.

### High Issues (4)

**TASK-01: TWDT Not Configured**
- **Impact:** Task starvation or deadlock goes undetected
- **Risk:** System hangs requiring manual reset
- **Fix Effort:** Low (sdkconfig.defaults + task subscription)
- **Priority:** P1

**TASK-02: LXStamper CPU Hogging**
- **Impact:** UI freeze during stamp generation (minutes)
- **Risk:** Poor user experience, potential watchdog triggers
- **Fix Effort:** Low (increase yield frequency)
- **Priority:** P2

**NIMBLE-01: Pending Queues Not Thread-Safe**
- **Impact:** Potential data corruption in handshake/data queues
- **Risk:** Crash if callback and loop access simultaneously
- **Fix Effort:** Low (add lock_guard in callbacks)
- **Priority:** P2

**NIMBLE-02: Shutdown During Active Operations**
- **Impact:** Use-after-free if callbacks fire during cleanup
- **Risk:** Crash during BLE restart/shutdown
- **Fix Effort:** Medium (add shutdown state, wait for ops)
- **Priority:** P2

### Medium Issues (9)

| ID | Issue | Fix Effort | Priority |
|----|-------|------------|----------|
| LVGL-01/02/03 | Missing LVGL_LOCK in 3 screen classes | Low | P3 |
| NIMBLE-03 | Soft reset resource leak | Medium | P3 |
| NIMBLE-04 | Silent mutex timeout failure | Low | P3 |
| NIMBLE-05 | Cache eviction of connected devices | Low | P3 |
| TASK-03 | LVGL mutex infinite wait | Low | P3 |
| TASK-04 | I2S infinite wait | Low | P3 |
| MUTEX-01 | No formal ordering enforcement | Low (docs) | P4 |

### Low Issues (5)

| ID | Issue | Action |
|----|-------|--------|
| NIMBLE-06 | Volatile for complex state | Document risk |
| NIMBLE-07 | Undocumented 50ms delay | Add documentation |
| TASK-05 | Link watchdog TODO | Implement when needed |
| MUTEX-02 | portMAX_DELAY masking | Add debug timeout |

---

## Cross-Subsystem Concerns

### LVGL <-> BLE Interaction Pattern

```
+----------------+    Message Queue    +----------------+
|                | -----------------> |                |
|  BLE Callbacks |   (deferred work)  |  UI Updates    |
|  (BLE mutex)   |                    |  (LVGL mutex)  |
|                | <----------------- |                |
+----------------+    LXMF Router     +----------------+
```

**Current Design (SAFE):**
- BLE callbacks push to `_pending_data` queue
- BLE task processes queue with `_mutex` held
- Callbacks notify LXMRouter (no UI mutex)
- Main task updates UI with LVGL_LOCK()
- **Mutexes never nested across subsystems**

**Risk Area:** NIMBLE-01 identifies that `_pending_data.push_back()` is called from NimBLE callbacks WITHOUT holding `_mutex`. This is the only thread-safety gap in the interaction.

### Callback Threading (NimBLE -> Deferred -> LVGL)

```
NimBLE Host Task                BLE Task                    Main Task
      |                            |                            |
      | onWrite() callback         |                            |
      | (no mutex)                 |                            |
      |--------------------------->|                            |
      | push_back(pending_data)    |                            |
      |                            |                            |
      |                            | loop() with _mutex         |
      |                            |-------------------------->|
      |                            | processReceivedData()     |
      |                            |                           |
      |                            |                           | UIManager::update()
      |                            |                           | with LVGL_LOCK()
      |                            |                           |
```

**Analysis:**
- NimBLE callbacks run in NimBLE host task (8KB stack)
- BLE task processes deferred work (8KB stack)
- Main task handles UI updates (8KB stack)
- No task holds multiple mutexes simultaneously

### Mutex Ordering Across Layers

Documented in 04-MUTEX.md:

```
Level 1: LVGL Mutex (UI - outermost)
Level 2: BLE Interface Mutex (Application)
Level 3: NimBLE Conn Mutex (Platform)
Level 4: State Spinlocks (Atomic - innermost)
```

**Enforcement:** Currently implicit via code structure. Recommend adding static analysis in Phase 5.

---

## Phase 5 Backlog Items

### Priority 1 - Critical (Fix Immediately)

| ID | Issue | Effort | Source |
|----|-------|--------|--------|
| P1-1 | Enable TWDT with 10s timeout, subscribe critical tasks | Low | TASK-01 |

### Priority 2 - High (Fix Before Production)

| ID | Issue | Effort | Source |
|----|-------|--------|--------|
| P2-1 | Add thread-safe access to BLE pending queues | Low | NIMBLE-01 |
| P2-2 | Add graceful shutdown with operation wait | Medium | NIMBLE-02 |
| P2-3 | Improve LXStamper yield frequency (every 10 rounds) | Low | TASK-02 |

### Priority 3 - Medium (Fix When Convenient)

| ID | Issue | Effort | Source |
|----|-------|--------|--------|
| P3-1 | Add LVGL_LOCK to 3 screen constructors/destructors | Low | LVGL-01/02/03 |
| P3-2 | Add timeout to LVGL mutex acquisition (debug mode) | Low | TASK-03 |
| P3-3 | Add timeout to I2S audio writes | Low | TASK-04 |
| P3-4 | Add logging for connection mutex timeout | Low | NIMBLE-04 |
| P3-5 | Check connection status before cache eviction | Low | NIMBLE-05 |
| P3-6 | Consider hard recovery with deinit/init cycle | Medium | NIMBLE-03 |

### Priority 4 - Low (Document/Defer)

| ID | Issue | Effort | Source |
|----|-------|--------|--------|
| P4-1 | Implement Link watchdog with FreeRTOS timer | Medium | TASK-05 |
| P4-2 | Document 50ms recovery delay rationale | Low | NIMBLE-07 |
| P4-3 | Add debug-mode mutex ordering assertions | Low | MUTEX-01 |
| P4-4 | Document volatile usage in native GAP handler | Low | NIMBLE-06 |

---

## Recommendations

### Immediate Actions

1. **Enable TWDT in sdkconfig.defaults:**
   ```
   CONFIG_ESP_TASK_WDT=y
   CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
   CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
   CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y
   ```

2. **Subscribe critical tasks to TWDT:**
   ```cpp
   #include "esp_task_wdt.h"
   esp_task_wdt_add(_task_handle);  // In task creation
   esp_task_wdt_reset();            // In task loop
   ```

3. **Fix pending queue thread safety:**
   ```cpp
   void BLEInterface::onHandshakeComplete(...) {
       std::lock_guard<std::recursive_mutex> lock(_mutex);
       _pending_handshakes.push_back(pending);
   }
   ```

### Short-term Improvements

1. **Add timeout variants for debug builds:**
   ```cpp
   #ifdef DEBUG
   if (xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
       ERROR("Mutex timeout - possible deadlock");
       esp_task_wdt_reset();  // Force watchdog trigger
   }
   #endif
   ```

2. **Improve LXStamper yield frequency:**
   ```cpp
   if (rounds % 10 == 0) {  // Was 100
       vTaskDelay(1);
       esp_task_wdt_reset();
   }
   ```

### Documentation Updates

1. Add threading model to code comments
2. Document mutex ordering in CONTRIBUTING.md
3. Add code review checklist for concurrency

---

## Architecture Diagram

```
                           ESP32-S3 Concurrency Architecture
+-----------------------------------------------------------------------------+
|                                                                             |
|  CORE 0                              CORE 1                                 |
|  +---------------------------+       +---------------------------+          |
|  |        BLE Task           |       |       LVGL Task           |          |
|  |   +-------------------+   |       |   +-------------------+   |          |
|  |   | BLEInterface      |   |       |   | lv_task_handler() |   |          |
|  |   | loop()            |   |       |   |                   |   |          |
|  |   +--------+----------+   |       |   +--------+----------+   |          |
|  |            |              |       |            |              |          |
|  |   +--------v----------+   |       |   +--------v----------+   |          |
|  |   | _mutex            |   |       |   | _mutex (LVGL)     |   |          |
|  |   | (std::recursive)  |   |       |   | (recursive sem)   |   |          |
|  |   +-------------------+   |       |   +-------------------+   |          |
|  |                           |       |                           |          |
|  |   +-------------------+   |       +---------------------------+          |
|  |   | NimBLE Platform   |   |                                              |
|  |   | +---------------+ |   |                                              |
|  |   | | _state_mux    | |   |       CORE 0/1 (not pinned)                  |
|  |   | | (spinlock)    | |   |       +---------------------------+          |
|  |   | +---------------+ |   |       |      Main Task            |          |
|  |   | | _conn_mutex   | |   |       |   +-------------------+   |          |
|  |   | | (100ms)       | |   |       |   | Reticulum loop()  |   |          |
|  |   | +---------------+ |   |       |   | Interface loops() |   |          |
|  |   +-------------------+   |       |   | UIManager update()|   |          |
|  +---------------------------+       |   +--------+----------+   |          |
|                                      |            |              |          |
|                                      |   +--------v----------+   |          |
|                                      |   | LVGL_LOCK()       |   |          |
|                                      |   | (shared w/LVGL)   |   |          |
|                                      |   +-------------------+   |          |
|                                      +---------------------------+          |
|                                                                             |
|  +-----------------------------------------------------------------------+  |
|  |                         Shared Resources                              |  |
|  |  +---------------------+  +---------------------+  +----------------+ |  |
|  |  | LVGL Mutex          |  | BLE Interface Mutex |  | SPI Mutex      | |  |
|  |  | (recursive)         |  | (recursive)         |  | (binary)       | |  |
|  |  | holders: LVGL, Main |  | holders: BLE,       |  | holder: Main   | |  |
|  |  | timeout: infinite   |  | NimBLE callbacks    |  | timeout: 1s    | |  |
|  |  +---------------------+  +---------------------+  +----------------+ |  |
|  +-----------------------------------------------------------------------+  |
|                                                                             |
|  +-----------------------------------------------------------------------+  |
|  |                      Message Queues (Decoupling)                      |  |
|  |  _pending_handshakes  <--  NimBLE callbacks                          |  |
|  |  _pending_data        <--  NimBLE onWrite                            |  |
|  |  LXMRouter queue      <--  BLE task (processed data)                 |  |
|  +-----------------------------------------------------------------------+  |
|                                                                             |
+-----------------------------------------------------------------------------+

ISSUE LOCATIONS:
  [!] NIMBLE-01: _pending_* push without lock (callback -> queue)
  [!] TASK-01: No TWDT subscription for any application task
  [!] TASK-02: LXStamper yields too infrequently (in Main task)
```

---

## CONC Requirements Status

| Requirement | Description | Status | Document |
|-------------|-------------|--------|----------|
| CONC-01 | Audit LVGL Mutex Usage | Complete | 04-LVGL.md |
| CONC-02 | Audit NimBLE Lifecycle | Complete | 04-NIMBLE.md |
| CONC-03 | Audit FreeRTOS Tasks | Complete | 04-TASKS.md |
| CONC-04 | Document Mutex Ordering | Complete | 04-MUTEX.md |
| CONC-05 | Verify Stack Sizes | Complete | 04-TASKS.md |

All Phase 4 requirements have been addressed.

---

## Next Steps

1. **Phase 5 Implementation:** Begin fixing issues based on priority backlog
2. **TWDT Configuration:** Highest priority - enables detection of future issues
3. **Thread Safety Fixes:** NIMBLE-01 should be fixed before extended testing
4. **Documentation:** Update codebase with threading assumptions

---

*Phase 4 Concurrency Audit Complete*
*Generated: 2026-01-24*
