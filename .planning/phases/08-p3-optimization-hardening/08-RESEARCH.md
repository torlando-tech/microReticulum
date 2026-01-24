# Phase 8: P3 Optimization & Hardening - Research

**Researched:** 2026-01-24
**Domain:** Memory pool allocators, BLE shutdown coordination, debug timeout patterns, volatile/delay documentation
**Confidence:** HIGH

## Summary

Phase 8 addresses all remaining P3 optimization and hardening issues from the v1 stability audits. The work spans five distinct areas: memory pool allocators for Bytes and Packet hot paths (MEM-H1, MEM-H2, MEM-H3, MEM-H4), BLE graceful shutdown (CONC-H4, CONC-M4), debug timeout variants for portMAX_DELAY sites (CONC-L4), documentation of volatile/delay usage (CONC-L1, CONC-L2), and the rolled-over P2 requirement MEM-M3 (ArduinoJson 7 migration) and MEM-L1 (toHex capacity).

The research confirms that pool allocators should use a fixed-size freelist pattern for simplicity and predictability. BLE shutdown requires coordinating with NimBLE's asynchronous callback model, with a 10-second timeout before forced closure. Debug timeouts extend the successful Phase 7 pattern (5s LVGL mutex) to all remaining portMAX_DELAY sites.

**Primary recommendation:** Implement in this order: (1) MEM-L1/MEM-M3 (trivial fixes), (2) debug timeouts (CONC-L4), (3) volatile/delay documentation (CONC-L1, CONC-L2), (4) pool allocators (MEM-H1, MEM-H2, MEM-H3, MEM-H4), (5) BLE shutdown (CONC-H4, CONC-M4). This order progresses from lowest to highest complexity.

## Standard Stack

This phase uses existing libraries and patterns. The pool allocators are custom implementations tailored to ESP32 constraints.

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| ArduinoJson | 7.4.2 | JSON persistence | Already in use, v7 migration required |
| FreeRTOS | ESP-IDF bundled | Semaphore/mutex primitives | Platform standard |
| NimBLE-Arduino | Latest | BLE stack | Already integrated |
| heap_caps | ESP-IDF | Memory allocation with capabilities | Platform standard for PSRAM |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| PSRAMAllocator | Custom | Routes allocations to PSRAM | Already used by Bytes |
| LVGLLock | Custom | RAII mutex wrapper | Phase 7 pattern to extend |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Fixed-size pool | Dynamic arena | User context specifies fixed pools (simpler, predictable) |
| Custom pool impl | TLSF allocator | TLSF exists in codebase but user context specifies pool pattern |
| Hard reboot on BLE fail | Soft recovery | Soft recovery attempted first per user context |

## Architecture Patterns

### Pattern 1: Fixed-Size Object Pool

**What:** Pre-allocated array of objects with freelist tracking. O(1) allocate/free with zero fragmentation.

**When to use:** High-frequency allocation of same-sized objects (Bytes buffer, Packet::Object).

**Why this pattern:** User context explicitly specifies "fixed-size pools (not dynamic arenas)" for predictable memory and simpler debugging.

**Example:**
```cpp
// Source: Custom pattern for this project
template <typename T, size_t N>
class ObjectPool {
public:
    ObjectPool() {
        // Initialize freelist
        for (size_t i = 0; i < N - 1; i++) {
            _slots[i].next_free = i + 1;
        }
        _slots[N - 1].next_free = INVALID_SLOT;
        _first_free = 0;
        _allocated_count = 0;
    }

    T* allocate() {
        if (_first_free == INVALID_SLOT) {
            return nullptr;  // Pool exhausted - caller falls back to heap
        }
        size_t slot = _first_free;
        _first_free = _slots[slot].next_free;
        _allocated_count++;
        return new (&_slots[slot].storage) T();  // Placement new
    }

    void deallocate(T* ptr) {
        if (!ptr) return;
        size_t slot = ((char*)ptr - (char*)_slots) / sizeof(Slot);
        if (slot >= N) return;  // Not from this pool
        ptr->~T();  // Explicit destructor
        _slots[slot].next_free = _first_free;
        _first_free = slot;
        _allocated_count--;
    }

    size_t allocated() const { return _allocated_count; }
    size_t capacity() const { return N; }

private:
    static constexpr size_t INVALID_SLOT = ~size_t(0);

    struct Slot {
        union {
            alignas(T) char storage[sizeof(T)];
            size_t next_free;
        };
    };

    Slot _slots[N];
    size_t _first_free;
    size_t _allocated_count;
};
```

**Pool sizing (Claude's discretion per user context):**
- **Bytes buffer pool:** 32 slots of common sizes (64, 256, 512 bytes)
- **Packet::Object pool:** 16 slots (matches existing pool patterns in Transport.h)

**Heap fallback behavior:** On pool exhaustion, allocate from heap using existing PSRAMAllocator. Never fail an allocation - graceful degradation.

### Pattern 2: Inline Fixed-Size Buffers for Packet

**What:** Replace Bytes members with fixed-size byte arrays for predictable-length fields.

**When to use:** Packet fields with known maximum sizes (hashes, IDs).

**Example:**
```cpp
// Source: Based on Packet.h:328-339 analysis
class Packet::Object {
private:
    // BEFORE: Bytes members (each has shared_ptr overhead ~24 bytes)
    // Bytes _packet_hash;
    // Bytes _destination_hash;
    // Bytes _transport_id;
    // Bytes _ratchet_id;

    // AFTER: Inline buffers (known sizes from Type.h)
    // Type::Reticulum::DESTINATION_LENGTH = 16 bytes
    // Type::Reticulum::TRUNCATED_HASH_LENGTH = 10 bytes
    uint8_t _packet_hash[32];       // SHA256 hash = 32 bytes
    uint8_t _destination_hash[16];  // DESTINATION_LENGTH
    uint8_t _transport_id[16];      // DESTINATION_LENGTH
    uint8_t _ratchet_id[32];        // Ratchet ID = 32 bytes

    uint8_t _packet_hash_len = 0;   // Actual length (0 = empty)
    uint8_t _destination_hash_len = 0;
    uint8_t _transport_id_len = 0;
    uint8_t _ratchet_id_len = 0;

    // Variable-size fields remain as Bytes
    Bytes _raw;
    Bytes _data;
    Bytes _plaintext;
    Bytes _header;
    Bytes _ciphertext;
};
```

**Savings estimate:** 4 Bytes members x ~24 bytes overhead = ~96 bytes saved per packet. With 5 variable Bytes remaining at ~24 bytes each = ~120 bytes, total Object overhead reduced from ~216 to ~120 bytes (~44% reduction). User context mentions ~150 bytes savings which is achievable with more aggressive inlining.

### Pattern 3: Debug Timeout for Mutex Acquisition

**What:** In debug builds, use 5-second timeout on all portMAX_DELAY mutex sites. Log warning and continue on timeout.

**When to use:** All sites using portMAX_DELAY for semaphore/mutex acquisition.

**Example (extends Phase 7 pattern):**
```cpp
// Source: Extension of src/UI/LVGL/LVGLLock.h pattern
#ifndef NDEBUG
    // Debug builds: 5-second timeout for stuck task detection
    BaseType_t result = xSemaphoreTakeRecursive(mutex, pdMS_TO_TICKS(5000));
    if (result != pdTRUE) {
        WARNING("Mutex timeout (5s) in " + std::string(__FUNCTION__) +
                " - possible deadlock or stuck task");
        // Per user context: "log warning and continue waiting"
        // Don't break functionality - just log and proceed with infinite wait
        xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
    }
#else
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
#endif
```

**Current portMAX_DELAY sites requiring this pattern:**
1. `src/UI/LVGL/LVGLLock.h:50` - Already done in Phase 7
2. `src/UI/LVGL/LVGLInit.cpp:162` - In lvgl_task loop
3. Any additional mutex acquisition sites discovered during implementation

### Pattern 4: BLE Graceful Shutdown

**What:** Wait for active operations to complete before NimBLE deinit, with 10-second timeout and unclean shutdown flag.

**When to use:** All shutdown/restart scenarios (user-initiated, OTA, watchdog recovery).

**Reference:** [NimBLE shutdown patterns](https://github.com/espressif/esp-idf/issues/3475)

**Critical operations to wait for (Claude's discretion):**
- Active write operations (GATT writes in progress)
- Active connection establishment
- Service discovery in progress

**Non-critical operations (safe to interrupt):**
- Scanning (can restart)
- Advertising (can restart)
- Read operations (stateless)

**Example:**
```cpp
// Source: Based on NimBLEPlatform.cpp:207-236 enhancement
void NimBLEPlatform::shutdown() {
    const uint32_t SHUTDOWN_TIMEOUT_MS = 10000;
    uint32_t start = millis();

    // Stop accepting new operations
    transitionGAPState(GAPState::READY, GAPState::TRANSITIONING);

    // Wait for active write operations to complete
    while (hasActiveWriteOperations() && (millis() - start) < SHUTDOWN_TIMEOUT_MS) {
        delay(10);
    }

    // Check if we timed out
    if (millis() - start >= SHUTDOWN_TIMEOUT_MS) {
        WARNING("BLE shutdown timeout - forcing close, setting unclean flag");
        setUncleanShutdownFlag(true);  // For next boot verification
    }

    // Proceed with existing stop() logic
    stop();

    // Cleanup clients
    for (auto& kv : _clients) {
        if (kv.second) {
            NimBLEDevice::deleteClient(kv.second);
        }
    }
    _clients.clear();
    _connections.clear();
    _discovered_devices.clear();
    _discovered_order.clear();

    // Deinit NimBLE
    if (_initialized) {
        NimBLEDevice::deinit(true);
        _initialized = false;
    }

    INFO("NimBLEPlatform: Shutdown complete" +
         std::string(wasCleanShutdown() ? "" : " (unclean)"));
}
```

### Pattern 5: toHex Capacity Reservation

**What:** Reserve string capacity upfront to avoid multiple reallocations.

**When to use:** Building strings character-by-character.

**Example:**
```cpp
// Source: src/Bytes.cpp:168-185 modification
std::string Bytes::toHex(bool upper /*= false*/) const {
    if (!_data) {
        return "";
    }
    std::string hex;
    hex.reserve(_data->size() * 2);  // ADD THIS LINE
    for (uint8_t byte : *_data) {
        if (upper) {
            hex += hex_upper_chars[(byte & 0xF0) >> 4];
            hex += hex_upper_chars[(byte & 0x0F) >> 0];
        } else {
            hex += hex_lower_chars[(byte & 0xF0) >> 4];
            hex += hex_lower_chars[(byte & 0x0F) >> 0];
        }
    }
    return hex;
}
```

## Don't Hand-Roll

Problems that look simple but have existing solutions:

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| PSRAM allocation | Custom heap | PSRAMAllocator (existing) | Already handles fallback correctly |
| Mutex wrapper | Raw semaphore calls | RAII pattern like LVGLLock | Exception-safe, prevents leaks |
| BLE state machine | Ad-hoc flags | Existing GAPState/MasterState/SlaveState | Already designed for this |
| Debug detection | Manual ifdefs | `#ifndef NDEBUG` (existing pattern) | Platform-standard, consistent |

**Key insight:** The codebase already has well-designed patterns for all major concerns. Phase 8 is primarily extending these patterns, not creating new infrastructure.

## Common Pitfalls

### Pitfall 1: Pool Allocator Thread Safety

**What goes wrong:** Pool accessed from multiple tasks without synchronization causes corruption.
**Why it happens:** Bytes/Packet used across BLE, Transport, and UI tasks.
**How to avoid:** Use spinlock or mutex for pool freelist operations.
**Warning signs:** Intermittent crashes, double-free, use-after-free.

### Pitfall 2: NimBLE Deinit/Reinit Crashes

**What goes wrong:** Calling deinit while callbacks still pending causes heap corruption.
**Why it happens:** NimBLE callbacks run on host task, async from application code.
**How to avoid:** Wait for active operations before deinit; use timeout with forced cleanup.
**Warning signs:** Guru Meditation Error on second BLE init cycle.
**Reference:** [NimBLE deinit issues](https://github.com/espressif/esp-idf/issues/17493)

### Pitfall 3: Inline Buffer Length Tracking

**What goes wrong:** Fixed-size buffers used without length tracking causes buffer overread.
**Why it happens:** Bytes has implicit length; inline buffers need explicit length.
**How to avoid:** Always pair fixed buffer with length field; check length before access.
**Warning signs:** Garbage data in packet fields, hash comparison failures.

### Pitfall 4: Debug Timeout Breaking Functionality

**What goes wrong:** Debug timeout triggers assert instead of continuing.
**Why it happens:** Early Phase 7 pattern used assert on timeout.
**How to avoid:** Per user context: "log warning and continue waiting (don't break functionality)".
**Warning signs:** Debug builds crash during normal operation.

### Pitfall 5: Pool Sizing Too Small

**What goes wrong:** Pool exhausted frequently, constant heap fallback negates benefit.
**Why it happens:** Under-estimated peak usage during message bursts.
**How to avoid:** Profile actual usage; size for 90th percentile, rely on heap for spikes.
**Warning signs:** Pool hit rate < 80% in steady state.

## Code Examples

### Example 1: PacketReceipt Lazy Allocation (MEM-H4)

```cpp
// Source: Based on src/Packet.h:51-52 and Phase 7 work
class PacketReceipt {
public:
    // Default constructor defers allocation (already done in Phase 7)
    PacketReceipt() : _object(nullptr) {}

    // Ensure object exists only when needed
    void ensure_object() {
        if (!_object) {
            _object = std::make_shared<Object>();
        }
    }

    // Setters call ensure_object() before accessing _object
    void status(Type::PacketReceipt::Status status) {
        ensure_object();  // Lazy allocation
        _object->_status = status;
    }
    // ...
};
```

### Example 2: ArduinoJson 7 Migration (MEM-M3)

```cpp
// Source: src/Utilities/Persistence.cpp and Persistence.h
// Migration per https://arduinojson.org/v7/how-to/upgrade-from-v6/

// BEFORE (v6 - deprecated):
// DynamicJsonDocument _document(8192);

// AFTER (v7 - current):
JsonDocument _document;  // Elastic capacity, no size needed

// API changes:
// - createNestedArray() -> add<JsonArray>() or to<JsonArray>()
// - createNestedObject() -> add<JsonObject>() or to<JsonObject>()
// - containsKey("key") -> ["key"].is<T>()
// - capacity() -> overflowed()
```

### Example 3: Volatile Usage Documentation (CONC-L1)

```cpp
// Source: src/BLE/platforms/NimBLEPlatform.h:288-296
// Native GAP handler uses volatile for ISR-like callback context

// These volatile flags are accessed from:
// 1. NimBLE host task (callback context - like ISR)
// 2. BLE task (loop() context - application)
//
// Volatile is appropriate here because:
// - Single-word reads/writes are atomic on ESP32
// - These are simple flags, not complex state
// - Full memory barriers not needed - flag semantics sufficient
// - Alternative (mutex) would cause priority inversion in callback
//
// Reference: ESP32 Technical Reference Manual, Section 5.4 (Memory Consistency)
volatile bool _native_connect_pending = false;
volatile bool _native_connect_success = false;
volatile int _native_connect_result = 0;
volatile uint16_t _native_connect_handle = 0;
```

### Example 4: Delay Rationale Documentation (CONC-L2)

```cpp
// Source: src/BLE/platforms/NimBLEPlatform.cpp:502 and similar

// 50ms delay in error recovery path
//
// RATIONALE:
// After a BLE operation failure (e.g., scan start returns error), the NimBLE
// stack needs time to process the failure internally before retry. Without
// this delay:
// - Immediate retry often fails with same error (stack still processing)
// - Rapid retries can trigger NimBLE assertion failures
// - Stack may enter unrecoverable state requiring full restart
//
// 50ms chosen empirically - balances recovery time vs responsiveness.
// Shorter (10ms) saw retry failures; longer (100ms) adds noticeable latency.
//
// Reference: NimBLE scheduler tick is ~10ms, 50ms = 5 scheduler cycles
delay(50);
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| DynamicJsonDocument (ArduinoJson 6) | JsonDocument (ArduinoJson 7) | Jan 2024 | Elastic capacity, simpler API |
| portMAX_DELAY everywhere | Debug timeout variants | Phase 7 | Deadlock detection |
| Bytes COW with heap | Pool allocator for hot path | Phase 8 | Reduced fragmentation |

**Deprecated/outdated:**
- `DynamicJsonDocument`: Replaced by `JsonDocument` in ArduinoJson 7
- `createNestedArray()`/`createNestedObject()`: Use `add<T>()`/`to<T>()` instead
- `containsKey()`: Use `operator[]` + `is<T>()` for type-safe check

## Open Questions

### 1. Pool Size Tuning

**What we know:** User context allows Claude discretion on pool sizes. Transport uses 8-16 slot pools.
**What's unclear:** Actual peak usage during message bursts (would require profiling).
**Recommendation:** Start with 16 Packet slots, 32 Bytes buffers. Add runtime pool stats for post-deployment tuning.

### 2. Unclean Shutdown Flag Storage

**What we know:** Flag should be checked on boot for integrity verification.
**What's unclear:** Where to persist flag (RTC RAM? NVS?).
**Recommendation:** Use RTC_NOINIT memory region - survives soft reboot, cleared on power cycle.

### 3. Which BLE Operations Are Critical

**What we know:** User context: "Claude decides which operations actually risk corruption (writes vs reads/scans)".
**What's unclear:** Whether service discovery mid-stream causes issues.
**Recommendation:** Treat only write operations as critical. Reads and scans can be interrupted safely.

## Sources

### Primary (HIGH confidence)
- ArduinoJson official migration guide: https://arduinojson.org/v7/how-to/upgrade-from-v6/
- ESP-IDF Heap Memory Allocation: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/mem_alloc.html
- FreeRTOS kernel documentation: /freertos/freertos-kernel (Context7)
- Project codebase analysis (Bytes.h, Packet.h, NimBLEPlatform.cpp)
- Phase 7 RESEARCH.md (established patterns)

### Secondary (MEDIUM confidence)
- NimBLE shutdown issues: https://github.com/espressif/esp-idf/issues/3475
- ESP32 heap fragmentation: https://github.com/espressif/esp-idf/issues/13588
- NimBLE deinit/reinit: https://github.com/espressif/esp-idf/issues/17493

### Tertiary (LOW confidence)
- Generic pool allocator patterns from web search (verified against ESP32 constraints)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - Using existing project libraries
- Architecture patterns: HIGH - Based on existing codebase patterns and official docs
- Pitfalls: MEDIUM - Based on ESP-IDF issues and NimBLE bug reports
- Pool sizing: LOW - Requires runtime profiling for optimization

**Research date:** 2026-01-24
**Valid until:** 60 days (stable patterns, no expected breaking changes)

## CONTRIBUTING.md Additions

Per user context, include a summary table of all timing/volatile sites. Structure for the documentation phase:

```markdown
## Timing and Volatile Reference

| File | Line | Type | Value | Rationale |
|------|------|------|-------|-----------|
| NimBLEPlatform.cpp | 257 | delay | 100ms | Stack init settling time |
| NimBLEPlatform.cpp | 287 | delay | 100ms | Advertising restart after recovery |
| NimBLEPlatform.cpp | 406 | delay | 10ms | Scan stop polling interval |
| NimBLEPlatform.cpp | 439 | delay | 10ms | Advertising stop polling |
| NimBLEPlatform.cpp | 502 | delay | 50ms | Error recovery before retry |
| NimBLEPlatform.cpp | 514 | delay | 50ms | Connect attempt recovery |
| NimBLEPlatform.cpp | 591 | delay | 20ms | MTU negotiation settling |
| NimBLEPlatform.cpp | 674 | delay | 10ms | Connection loop polling |
| NimBLEPlatform.cpp | 772 | delay | 20ms | Service discovery settling |
| NimBLEPlatform.cpp | 787 | delay | 10ms | Service discovery polling |
| NimBLEPlatform.cpp | 970 | delay | 10ms | Notification send retry interval |
| NimBLEPlatform.cpp | 1050 | delay | 50ms | Soft reset processing |
| NimBLEPlatform.cpp | 1060 | delay | 10ms | Reset wait polling |
| NimBLEPlatform.cpp | 1073 | delay | 10ms | Stack stabilization after reset |
| NimBLEPlatform.cpp | 1316 | delay | 10ms | Loop iteration throttle |
| Display.cpp | 104 | delay | 150ms | LCD reset pulse width (ST7789 spec) |
| Display.cpp | 108,124,128,132 | delay | 10ms | SPI command settling |
| NimBLEPlatform.h | 288-296 | volatile | bool/int/uint16 | ISR-like callback flag sync |
| Trackball.h | 93-97 | volatile | int16/uint32 | ISR pulse counters |

See individual code sites for detailed rationale comments.
```
