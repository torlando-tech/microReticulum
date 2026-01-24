# Architecture Patterns for Stable ESP32-S3 FreeRTOS Firmware

**Domain:** Embedded ESP32-S3 firmware with PSRAM, FreeRTOS, BLE, LVGL UI
**Researched:** 2026-01-23
**Overall Confidence:** HIGH (verified against ESP-IDF documentation and FreeRTOS kernel sources)

## Executive Summary

This document outlines architecture patterns for building stable, long-running ESP32-S3 firmware. The primary threats to stability in this environment are:

1. **Heap fragmentation** from dynamic allocation patterns incompatible with constrained memory
2. **Task starvation and priority inversion** leading to watchdog timeouts
3. **Uncontrolled object lifetime** causing memory leaks or use-after-free
4. **PSRAM misuse** causing cache coherency issues or DMA failures

The patterns below address each threat systematically, with specific audit implications for the microReticulum codebase.

---

## Memory Allocation Patterns

### Pattern 1: Static Allocation Preference

**What:** Allocate memory at compile time or during initialization, never during runtime operations.

**When:** All objects with deterministic lifetime (buffers, task stacks, protocol structures).

**Implementation:**
```cpp
// GOOD: Static allocation with fixed capacity
static Packet packet_pool[MAX_PACKETS];
static uint8_t tx_buffer[MTU_SIZE];

// BAD: Runtime allocation in hot path
void handle_packet() {
    Bytes* data = new Bytes(packet_size);  // Fragments heap
    // ...
}
```

**Trade-offs:**
- Pro: Zero fragmentation, deterministic memory footprint
- Con: Wastes memory when capacity unused, requires upfront sizing
- Con: Less flexible for variable workloads

**Audit Implication:** Search for `new` and `malloc` calls in packet handling, link establishment, and message routing paths. Each allocation should be justified or converted to pool-based.

---

### Pattern 2: Fixed-Size Memory Pools

**What:** Pre-allocate pools of identically-sized objects. Allocate from pool, return to pool.

**When:** Objects created/destroyed frequently with known maximum concurrency (Packets, Links, Destinations, BLE connections).

**Implementation:**
```cpp
// Pool with explicit slot management
template<typename T, size_t N>
class ObjectPool {
    T _slots[N];
    bool _in_use[N];
public:
    T* acquire() {
        for (size_t i = 0; i < N; i++) {
            if (!_in_use[i]) {
                _in_use[i] = true;
                return &_slots[i];
            }
        }
        return nullptr;  // Pool exhausted
    }
    void release(T* obj) {
        size_t idx = obj - _slots;
        if (idx < N) _in_use[idx] = false;
    }
};
```

**Trade-offs:**
- Pro: O(1) allocation, zero fragmentation, predictable memory usage
- Con: Fixed capacity requires overflow handling strategy
- Con: All objects same size, may waste memory for variable payloads

**Audit Implication:** Existing pools (`_announce_table_pool`, `_destination_table_pool`, `_known_destinations_pool`) need overflow detection. Check if pool exhaustion is detected and reported rather than silently failing.

**Reference:** The [Embedded Template Library (ETL)](https://www.etlcpp.com/pool.html) provides production-ready pool implementations designed for exactly this use case.

---

### Pattern 3: Capability-Based PSRAM Allocation

**What:** Use ESP-IDF heap capabilities to direct allocations to appropriate memory regions.

**When:** Large buffers (display framebuffers, message stores, crypto work buffers).

**Implementation:**
```cpp
// Large buffers go to PSRAM
lv_color_t* buf = (lv_color_t*)heap_caps_malloc(
    DISPLAY_BUF_SIZE,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
);

// DMA buffers must stay in internal RAM
uint8_t* dma_buf = (uint8_t*)heap_caps_malloc(
    DMA_SIZE,
    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
);

// Performance-critical small buffers in internal RAM
uint8_t* crypto_buf = (uint8_t*)heap_caps_malloc(
    AES_BLOCK_SIZE,
    MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL
);
```

**Trade-offs:**
- Pro: Preserves scarce internal RAM for DMA and performance-critical uses
- Con: PSRAM is slower (~1/4 speed of internal RAM)
- Con: Cache coherency issues if PSRAM data accessed during flash operations

**Audit Implication:** Current code uses `heap_caps_malloc(MALLOC_CAP_SPIRAM)` for display buffers and BZ2 compression - verify all large allocations follow this pattern. Check that DMA operations never use PSRAM buffers.

**Reference:** [ESP-IDF Heap Memory Allocation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/mem_alloc.html)

---

### Pattern 4: Allocation Size Thresholds

**What:** Configure automatic routing of allocations based on size thresholds.

**When:** Mixed allocation patterns where manual capability specification is impractical.

**Implementation:**
```cpp
// sdkconfig setting
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096

// Allocations <= 4KB prefer internal RAM
// Allocations > 4KB prefer PSRAM

// Reserve internal RAM for DMA-only allocations
// CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768
```

**Trade-offs:**
- Pro: Automatic optimization without code changes
- Con: May not match actual access patterns
- Con: Can still fragment if threshold poorly chosen

**Audit Implication:** Check platformio.ini and sdkconfig for these settings. If not configured, frequent small allocations may unnecessarily consume PSRAM.

---

## FreeRTOS Task Patterns

### Pattern 5: Task Pinning Strategy

**What:** Pin tasks to specific cores based on their characteristics and dependencies.

**When:** All task creation in ESP32 dual-core systems.

**Recommended Assignment:**
| Task Type | Core | Rationale |
|-----------|------|-----------|
| WiFi/BLE protocol | Core 0 | Required by ESP-IDF RF stack |
| LVGL UI | Core 1 | Avoid RF interference, consistent timing |
| Application logic | Core 1 | Keep Core 0 for protocol tasks |
| Time-critical ISR work | Core 0 or 1 | Pin to core where ISR runs |

**Implementation:**
```cpp
// Pin LVGL to Core 1, avoiding protocol core
xTaskCreatePinnedToCore(
    lvgl_task,
    "lvgl",
    8192,        // Stack size in bytes
    nullptr,
    5,           // Priority below lwIP (18) and WiFi/BT
    &handle,
    1            // Core 1 (APP_CPU)
);

// BLE should be on Core 0 with other protocol tasks
// But NimBLE manages this internally
```

**Trade-offs:**
- Pro: Predictable scheduling, avoids RF stack starvation
- Con: Unbalanced load if one core becomes overloaded
- Con: Floating-point operations auto-pin, may cause unexpected behavior

**Audit Implication:** Current LVGL task uses `xTaskCreatePinnedToCore` with core parameter. Verify BLE task affinity aligns with expectations. Check that no user tasks run at priority >= 18 on Core 0.

**Reference:** [FreeRTOS ESP32 Multicore](https://www.digikey.com/en/maker/projects/introduction-to-rtos-solution-to-part-12-multicore-systems/369936f5671d4207a2c954c0637e7d50)

---

### Pattern 6: Conservative Stack Sizing with Monitoring

**What:** Start with generous stacks, measure high water mark, reduce to measured + margin.

**When:** All task creation.

**Implementation:**
```cpp
// Initial generous allocation
#define LVGL_STACK_SIZE 8192
#define BLE_STACK_SIZE  4096

// Periodic monitoring during development
void monitor_stacks() {
    Serial.printf("LVGL stack free: %u bytes\n",
        uxTaskGetStackHighWaterMark(lvgl_handle));
    Serial.printf("BLE stack free: %u bytes\n",
        uxTaskGetStackHighWaterMark(ble_handle));
}

// After testing, reduce to: measured_usage + 512 bytes margin
```

**Stack Sizing Guidelines:**
- Tasks using printf/sprintf: +2KB minimum
- Tasks with recursion: +1KB per expected recursion depth
- Tasks calling crypto functions: +1KB for AES/SHA operations
- Minimum for any task: 2KB

**Trade-offs:**
- Pro: Prevents stack overflow crashes
- Con: Excessive stack wastes RAM
- Con: Stack high water mark only shows historical minimum, not worst case

**Audit Implication:** LVGL task uses 8KB which is appropriate. Verify all task stacks are sized based on measurement. Enable `CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK` for immediate crash detection on overflow.

**Reference:** [ESP-IDF RAM Usage Optimization](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/ram-usage.html)

---

### Pattern 7: Yield-Aware Task Design

**What:** Ensure all tasks yield regularly to prevent watchdog timeouts and starvation.

**When:** Any task with loops or blocking operations.

**Implementation:**
```cpp
// GOOD: Regular yields in processing loop
void processing_task(void* param) {
    while (true) {
        process_one_item();
        vTaskDelay(pdMS_TO_TICKS(1));  // Yield to scheduler
    }
}

// GOOD: Block on queue with timeout
void event_task(void* param) {
    Event evt;
    while (true) {
        if (xQueueReceive(queue, &evt, pdMS_TO_TICKS(100))) {
            handle_event(evt);
        }
        // Automatic yield during queue wait
    }
}

// BAD: Busy loop without yield
void bad_task(void* param) {
    while (!done) {
        check_condition();  // No yield, starves other tasks
    }
}
```

**Trade-offs:**
- Pro: Prevents watchdog timeouts, fair scheduling
- Con: Adds latency to tight loops
- Con: Minimum delay is 1 tick (typically 1ms)

**Audit Implication:** Search for `while` loops in tasks. Verify each has either `vTaskDelay`, `xQueueReceive`, `xSemaphoreTake`, or other blocking call. The LXMF stamper's `vTaskDelay(1)` is correct pattern.

**Reference:** [ESP32 Watchdogs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/wdts.html)

---

### Pattern 8: Mutex Priority Inheritance

**What:** Use mutexes instead of binary semaphores for resource protection to enable priority inheritance.

**When:** Shared resources accessed by tasks at different priorities.

**Implementation:**
```cpp
// GOOD: Mutex with automatic priority inheritance
SemaphoreHandle_t resource_mutex = xSemaphoreCreateMutex();

void high_priority_task() {
    if (xSemaphoreTake(resource_mutex, pdMS_TO_TICKS(100))) {
        // If low-priority task holds mutex, it inherits our priority
        use_resource();
        xSemaphoreGive(resource_mutex);
    }
}

// GOOD: Recursive mutex when same task may nest locks
SemaphoreHandle_t lvgl_mutex = xSemaphoreCreateRecursiveMutex();
```

**Trade-offs:**
- Pro: Prevents priority inversion (high-priority task blocked by low-priority)
- Con: Slight overhead vs. binary semaphore
- Con: Can still deadlock if lock ordering violated

**Audit Implication:** LVGL correctly uses recursive mutex. BLE uses regular mutex for connection map. Verify all shared resources use mutexes, not binary semaphores.

**Reference:** [FreeRTOS Mutexes and Priority Inheritance](https://github.com/freertos/freertos-kernel)

---

## Object Lifetime Patterns

### Pattern 9: Wrapper Pattern with Shared Pointer (Current Approach)

**What:** Public API class wraps `shared_ptr<Implementation>` for automatic lifetime management.

**When:** Objects with shared ownership semantics (Packet, Identity, Destination, Link).

**Current Implementation:**
```cpp
class Packet {
    std::shared_ptr<Object> _object;
public:
    Packet() : _object(std::make_shared<Object>()) {}
    // Copy shares ownership
    Packet(const Packet& other) : _object(other._object) {}
};
```

**Trade-offs:**
- Pro: Automatic memory management, prevents leaks
- Pro: Safe sharing across callbacks and async operations
- Con: Control block overhead (~16-24 bytes per shared_ptr)
- Con: Reference counting overhead on copy
- Con: Dynamic allocation for each object fragments heap

**Audit Implication:** This is the primary allocation pattern in the codebase. Each `std::make_shared` allocates from heap. Long-running operation creates thousands of packets, each allocating. Consider: Can packets be pooled with manual lifetime management?

**Reference:** [Memory Overhead of Smart Pointers](https://www.modernescpp.com/index.php/memory-and-performance-overhead-of-smart-pointer/)

---

### Pattern 10: Pool-Backed Smart Pointers (Recommended Migration)

**What:** Combine pool allocation with RAII wrapper for safe, fragmentation-free lifetime management.

**When:** Replacing heap-allocated shared_ptr objects in hot paths.

**Implementation:**
```cpp
// Pool-backed allocator for Packet::Object
template<typename T>
class PoolAllocator {
    static ObjectPool<T, POOL_SIZE>& pool() {
        static ObjectPool<T, POOL_SIZE> p;
        return p;
    }
public:
    T* allocate(size_t n) {
        return pool().acquire();
    }
    void deallocate(T* p, size_t) {
        pool().release(p);
    }
};

// Use with shared_ptr
using PacketPtr = std::shared_ptr<Packet::Object>;
PacketPtr make_packet() {
    return std::allocate_shared<Packet::Object>(
        PoolAllocator<Packet::Object>()
    );
}
```

**Trade-offs:**
- Pro: Zero heap fragmentation
- Pro: Preserves existing shared_ptr semantics
- Con: Requires custom allocator infrastructure
- Con: Pool exhaustion must be handled

**Audit Implication:** This is the recommended migration path. Identify the most frequently allocated object types (Packet, potentially Bytes) and prioritize for pool conversion.

---

### Pattern 11: Scoped Lifetime with LVGLLock Pattern

**What:** RAII wrapper acquires resource in constructor, releases in destructor.

**When:** Protecting critical sections, ensuring cleanup on all exit paths.

**Current Implementation:**
```cpp
class LVGLLock {
public:
    LVGLLock() {
        xSemaphoreTakeRecursive(LVGLInit::get_mutex(), portMAX_DELAY);
    }
    ~LVGLLock() {
        xSemaphoreGiveRecursive(LVGLInit::get_mutex());
    }
};

// Usage - lock released on any exit path
void update_ui() {
    LVGLLock lock;
    lv_label_set_text(label, "Hello");
    // lock released here, even if exception thrown
}
```

**Trade-offs:**
- Pro: Exception-safe, cannot forget to unlock
- Pro: Handles early returns correctly
- Con: Slight overhead for stack allocation of lock object

**Audit Implication:** Verify all LVGL API calls from non-LVGL tasks use LVGLLock. Search for `lv_` calls without surrounding LVGLLock.

---

## PSRAM Usage Patterns

### Pattern 12: PSRAM for Bulk Storage, Internal for Working Memory

**What:** Route allocations based on access patterns, not just size.

**When:** Any allocation decision.

**Guidelines:**
| Use Case | Memory Type | Reason |
|----------|-------------|--------|
| Display framebuffers | PSRAM | Large, sequential access |
| Message store | PSRAM | Large, infrequent access |
| Crypto keys/buffers | Internal | Performance-critical |
| DMA buffers | Internal only | DMA cannot access PSRAM reliably |
| BLE connection state | Internal | Frequent small accesses |
| Packet headers | Internal | Hot path, small size |
| Packet payloads | PSRAM if >256 bytes | Balance fragmentation vs. performance |

**Trade-offs:**
- Pro: Optimal use of limited internal RAM
- Con: Requires careful analysis of access patterns
- Con: PSRAM unavailable during flash operations

**Audit Implication:** BZ2 compression and display buffers correctly use PSRAM. Verify crypto operations use internal RAM. Check if large message payloads could be moved to PSRAM.

**Reference:** [ESP-IDF External RAM Support](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/external-ram.html)

---

### Pattern 13: Cache-Aware PSRAM Access

**What:** Avoid PSRAM access during flash operations; use aligned access for efficiency.

**When:** Any code that runs during OTA updates or flash writes.

**Implementation:**
```cpp
// BAD: PSRAM access during flash write causes cache exception
void flash_write_callback() {
    psram_buffer[0] = value;  // CRASH: Cache disabled during flash op
}

// GOOD: Copy to internal RAM before flash operation
void safe_flash_write(uint8_t* psram_data, size_t len) {
    uint8_t* internal_copy = (uint8_t*)heap_caps_malloc(
        len, MALLOC_CAP_INTERNAL);
    memcpy(internal_copy, psram_data, len);
    // Now safe to do flash operation
    do_flash_write(internal_copy, len);
    free(internal_copy);
}
```

**Trade-offs:**
- Pro: Prevents cache exception crashes
- Con: Requires extra copy and temporary buffer
- Con: Easy to forget during maintenance

**Audit Implication:** If OTA or flash filesystem operations are planned, audit all code paths for PSRAM access. Consider marking PSRAM buffers clearly in naming convention (`_psram_buf`).

---

## Anti-Patterns to Avoid

### Anti-Pattern 1: Frequent Small Heap Allocations

**What happens:** Each `new`/`malloc` of different sizes creates heap fragmentation. Over hours/days, heap becomes unusable even with free space.

**Detection:** `heap_caps_get_largest_free_block()` decreases over time while `heap_caps_get_free_size()` stays stable.

**In codebase:** Every `new Bytes()`, `new Object()` in wrapper constructors.

**Fix:** Pool allocation or stack allocation where possible.

---

### Anti-Pattern 2: Unbounded Queues and Collections

**What happens:** Collections grow until OOM crash.

**Detection:** Monitor collection sizes over time.

**In codebase:** Check `_destination_table`, `_announce_table`, BLE peer lists.

**Fix:** Fixed-size pools with LRU eviction.

---

### Anti-Pattern 3: Long Critical Sections

**What happens:** Interrupt watchdog triggers, other tasks starved.

**Detection:** IWDT timeout panics.

**In codebase:** Any code holding mutex while doing crypto or I/O.

**Fix:** Minimize critical section scope, defer work to task.

---

### Anti-Pattern 4: ISR Heap Allocation

**What happens:** Non-deterministic latency, potential deadlock.

**Detection:** Crashes from ISR context.

**In codebase:** Unlikely but check BLE callbacks.

**Fix:** Pre-allocate all buffers, use ring buffers for ISR->task communication.

---

### Anti-Pattern 5: Blocking in Callbacks

**What happens:** Stack exhaustion if callback called from deep call stack, deadlock if callback waits for resource held by caller.

**Detection:** Stack overflow crashes, deadlock analysis.

**In codebase:** Check announce handlers, packet callbacks, link callbacks.

**Fix:** Callbacks should only queue work, not execute it.

---

## Audit Focus Areas

Based on the patterns above, prioritize auditing these areas in the microReticulum codebase:

### High Priority

1. **Packet allocation path** (`Packet.cpp`, `Packet.h`)
   - Every packet creates heap allocation via `make_shared`
   - High frequency operation during message exchange
   - Candidate for pool-backed allocation

2. **Bytes class allocations** (`Bytes.cpp`, `Bytes.h`)
   - Underlying data container, allocated frequently
   - Check for copy-on-write efficiency
   - Consider PSRAM for large payloads

3. **BLE peer/connection management** (`BLEPeerManager.cpp`, `NimBLEPlatform.cpp`)
   - Fixed pools exist but overflow handling unclear
   - NimBLE has known memory leak issues in init/deinit cycles
   - Thread safety concerns with connection map

4. **Transport destination table** (`Transport.cpp`, `Transport.h`)
   - Linear search noted as performance bottleneck
   - Memory lifecycle unclear for culled entries
   - Pool slot management needs audit

### Medium Priority

5. **LVGL/BLE task synchronization**
   - Mutex usage appears correct
   - Verify no LVGL calls without lock
   - Check priority levels don't cause inversion

6. **Link state machine** (`Link.cpp`)
   - Complex state with many allocations
   - Resource timeout handling incomplete
   - Token allocation uses `new`

7. **LXMF message routing** (`LXMRouter.cpp`)
   - Router registry fixed pool
   - Message store persistence may allocate

### Lower Priority

8. **Crypto buffer management** (`Cryptography/*.cpp`)
   - AES buffer mutation documented as experimental
   - Check for stack vs. heap allocation
   - Verify no PSRAM for performance-critical paths

9. **UI screen lifecycle** (`UI/LXMF/*.cpp`)
   - Screens created once with `new`, not pooled
   - Acceptable if created at startup only
   - Check for dynamic allocations during operation

---

## Recommended Instrumentation

To validate patterns and detect issues during audit:

```cpp
// Periodic heap health check
void heap_health_check() {
    Serial.printf("Free heap: %u\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    Serial.printf("Largest block: %u\n", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    Serial.printf("Min free ever: %u\n", heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    Serial.printf("PSRAM free: %u\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// Task stack monitoring
void stack_health_check() {
    TaskStatus_t* task_status_array;
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    task_status_array = (TaskStatus_t*)pvPortMalloc(task_count * sizeof(TaskStatus_t));
    uxTaskGetSystemState(task_status_array, task_count, nullptr);
    for (UBaseType_t i = 0; i < task_count; i++) {
        Serial.printf("Task %s: stack high water %u\n",
            task_status_array[i].pcTaskName,
            task_status_array[i].usStackHighWaterMark);
    }
    vPortFree(task_status_array);
}
```

---

## Sources

### ESP-IDF Documentation (HIGH Confidence)
- [Minimizing RAM Usage - ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/ram-usage.html)
- [Heap Memory Allocation - ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/mem_alloc.html)
- [Support for External RAM](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/external-ram.html)
- [Watchdogs - ESP32](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/wdts.html)
- [FreeRTOS (IDF)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html)

### FreeRTOS Documentation (HIGH Confidence)
- [FreeRTOS Kernel GitHub](https://github.com/freertos/freertos-kernel) - Mutex priority inheritance, task management
- [FreeRTOS ESP32 Multicore](https://www.digikey.com/en/maker/projects/introduction-to-rtos-solution-to-part-12-multicore-systems/369936f5671d4207a2c954c0637e7d50)

### Embedded C++ Patterns (MEDIUM Confidence)
- [Memory Overhead of Smart Pointers](https://www.modernescpp.com/index.php/memory-and-performance-overhead-of-smart-pointer/)
- [Embedded Template Library (ETL)](https://www.etlcpp.com/) - Pool implementations
- [ETL ESP32 Component](https://github.com/marcel-cd/etlcpp.esp)

### ESP32 Community (MEDIUM Confidence)
- [ESP32 Forum: Memory Management Best Practices](https://www.esp32.com/viewtopic.php?t=20660)
- [ESP32 Forum: Heap Fragmentation](https://esp32.com/viewtopic.php?t=5646)
- [NimBLE-Arduino Issues](https://github.com/h2zero/NimBLE-Arduino/issues) - BLE stability concerns

---

*Architecture research: 2026-01-23*
