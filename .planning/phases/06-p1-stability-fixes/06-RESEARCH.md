# Phase 6: P1 Stability Fixes - Research

**Researched:** 2026-01-24
**Domain:** ESP32 memory management, FreeRTOS concurrency, C++ ODR compliance
**Confidence:** HIGH

## Summary

This phase addresses 5 P1 (highest priority) stability issues identified during the v1.1 Stability Quick Wins synthesis phase. The issues span memory management (ODR violation, vector pre-allocation) and concurrency (TWDT enablement, LXStamper yield frequency, BLE queue mutex protection).

All 5 fixes are low-effort (Trivial to Low) with high impact (WSJF >= 3.0). The fixes use standard ESP-IDF/FreeRTOS patterns already established in the codebase. No new dependencies are required.

**Primary recommendation:** Apply each fix in isolation with targeted verification, ensuring no functional regression. Enable TWDT first to catch any issues introduced by subsequent fixes.

## Standard Stack

This phase uses existing project dependencies - no new libraries required.

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| esp-idf | 5.x | ESP32 SDK, TWDT API | Official Espressif framework |
| FreeRTOS | Bundled | Task management, mutexes | ESP-IDF includes FreeRTOS |
| ArduinoJson | 7.4.2 | JSON serialization | Already in use (migration target) |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| esp_task_wdt.h | esp-idf | Task watchdog API | CONC-01: TWDT enablement |
| freertos/semphr.h | Bundled | Mutex primitives | Already used for LVGL_LOCK pattern |
| std::recursive_mutex | C++11 | BLE callback protection | Already used in BLEInterface |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| std::recursive_mutex | FreeRTOS semaphore | Consistency - std::mutex already in BLEInterface |
| esp_task_wdt | Custom timer | TWDT is hardware-backed, more reliable |

## Architecture Patterns

### Existing Patterns to Follow

The codebase already has well-established patterns for each fix type:

#### Pattern 1: Static Variable Definition (for MEM-01)

**What:** Single-definition-rule (ODR) compliant static variable placement
**When to use:** Class static or namespace-scope variables

**Current pattern (correct) in MessageStore.h:**
```cpp
// Header: extern declaration (no static keyword)
class MessageStore {
    static JsonDocument _json_doc;  // Declaration only
};

// Source: definition
JsonDocument MessageStore::_json_doc;  // Single definition
```

**Anti-pattern (current bug) in Persistence.h:**
```cpp
// Header: defines variable in header (BAD - included in multiple TUs)
namespace RNS { namespace Persistence {
    static DynamicJsonDocument _document(DOCUMENT_MAXSIZE);  // ODR violation!
}}

// Source: also defines (duplicate)
/*static*/ DynamicJsonDocument _document(DOCUMENT_MAXSIZE);  // Second definition
```

#### Pattern 2: Vector Pre-allocation (for MEM-02)

**What:** Reserve vector capacity at construction to prevent reallocations
**When to use:** When maximum size is known and bounded

**Example from BLEPeerManager (already in codebase):**
```cpp
// Fixed pool approach - no dynamic allocation at all
static constexpr size_t PEERS_POOL_SIZE = 8;
PeerByIdentitySlot _peers_by_identity_pool[PEERS_POOL_SIZE];
```

**For ResourceData vectors:**
```cpp
// In ResourceData constructor or accept() initialization
static constexpr size_t MAX_PARTS = 256;  // Based on MAX_EFFICIENT_SIZE / SDU
_parts.reserve(MAX_PARTS);
_hashmap.reserve(MAX_PARTS);
```

#### Pattern 3: TWDT Integration (for CONC-01)

**What:** ESP32 Task Watchdog Timer subscription and feeding
**When to use:** All long-running application tasks

**Source:** ESP-IDF official documentation (verified via Context7)

```cpp
// In sdkconfig.defaults:
CONFIG_ESP_TASK_WDT_EN=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y

// In task creation (e.g., LVGLInit.cpp, BLEInterface.cpp):
#include "esp_task_wdt.h"

// After xTaskCreate:
esp_task_wdt_add(_task_handle);

// In task loop:
while (true) {
    // ... do work ...
    esp_task_wdt_reset();  // Feed watchdog
    vTaskDelay(pdMS_TO_TICKS(5));
}
```

#### Pattern 4: Cooperative Yielding (for CONC-02)

**What:** Regular yield during CPU-intensive operations
**When to use:** Any loop that can run for >100ms

**Current pattern in LXStamper.cpp (line 195):**
```cpp
#ifdef ESP_PLATFORM
    if (rounds % 100 == 0) {  // Current: too infrequent
        vTaskDelay(1);
    }
#endif
```

**Improved pattern:**
```cpp
#ifdef ESP_PLATFORM
    if (rounds % 10 == 0) {   // Fix: yield 10x more often
        vTaskDelay(1);
        esp_task_wdt_reset();  // Also feed watchdog
    }
#endif
```

#### Pattern 5: Mutex-Protected Callback Queues (for CONC-03)

**What:** Lock acquisition before modifying shared state from callbacks
**When to use:** Any data structure accessed from multiple contexts

**Current pattern (partially correct) in BLEInterface.cpp:**
```cpp
// In loop() - correctly uses lock when READING:
if (!_pending_handshakes.empty()) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    for (const auto& pending : _pending_handshakes) {
        // process...
    }
    _pending_handshakes.clear();
}

// In callback - MISSING lock when WRITING:
void BLEInterface::onHandshakeComplete(...) {
    // BUG: No lock!
    _pending_handshakes.push_back(pending);  // Race condition
}
```

**Fixed pattern:**
```cpp
void BLEInterface::onHandshakeComplete(...) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_pending_handshakes.size() >= MAX_PENDING_HANDSHAKES) {
        WARNING("Pending handshake queue full");
        return;
    }
    _pending_handshakes.push_back(pending);
}
```

### Anti-Patterns to Avoid

- **Static definitions in headers:** Never use `static` for non-trivial objects in headers
- **Unbounded vector growth:** Always reserve() or use fixed pools for known-bounded data
- **Callback heavy work:** Defer to loop() via queues, but protect the queues
- **portMAX_DELAY everywhere:** Use timeouts in debug builds to catch deadlocks
- **Forgetting watchdog in loops:** Any long-running loop must yield and reset TWDT

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Task monitoring | Custom timer | esp_task_wdt | Hardware-backed, interrupt-safe |
| Thread-safe queue | Lock-free queue | std::lock_guard + vector | Simple, correct, already in use |
| LVGL thread safety | Manual mutex calls | LVGL_LOCK() macro | RAII pattern, exception-safe |

**Key insight:** The codebase already has correct patterns for all of these - the issues are inconsistent application, not missing abstractions.

## Common Pitfalls

### Pitfall 1: ODR Violation with Static Variables

**What goes wrong:** Linker creates multiple copies of static variable, causing initialization order issues and potential crashes
**Why it happens:** `static` in header means "internal linkage per TU" - each .cpp including the header gets its own copy
**How to avoid:** Single definition in .cpp file, `extern` declaration in header
**Warning signs:** Linker warnings about duplicate symbols, intermittent crashes

### Pitfall 2: Checking Queue Size Without Lock

**What goes wrong:** Size check and subsequent operation are not atomic - race condition
**Why it happens:** Developer thinks "empty()" is safe without lock
**How to avoid:** Always acquire lock before any operation on shared container
**Warning signs:** Sporadic crashes under high load, queue corruption

### Pitfall 3: TWDT Timeout Too Short

**What goes wrong:** Normal operations trigger watchdog reset
**Why it happens:** CPU-intensive operations (crypto, JSON) exceed timeout
**How to avoid:** Set timeout >= longest expected operation + margin (10s recommended)
**Warning signs:** Random resets during stamp generation or large file operations

### Pitfall 4: Missing Watchdog Reset in Yield

**What goes wrong:** Task yields but doesn't reset watchdog, timeout during idle
**Why it happens:** Developer adds vTaskDelay but forgets esp_task_wdt_reset()
**How to avoid:** Always pair yield with watchdog reset in subscribed tasks
**Warning signs:** Watchdog resets during otherwise normal operation

### Pitfall 5: Reserve vs Resize Confusion

**What goes wrong:** vector.reserve() allocates capacity but size() is still 0
**Why it happens:** Confusion between reserve (capacity) and resize (size)
**How to avoid:** Use reserve() for push_back patterns, resize() for index access
**Warning signs:** Accessing [i] after reserve() crashes, push_back works

## Code Examples

### MEM-01: Fix Duplicate Static Definition

```cpp
// src/Utilities/Persistence.h - BEFORE (line 475)
namespace RNS { namespace Persistence {
    static DynamicJsonDocument _document(Type::Persistence::DOCUMENT_MAXSIZE);
    static Bytes _buffer(Type::Persistence::BUFFER_MAXSIZE);
}}

// src/Utilities/Persistence.h - AFTER
namespace RNS { namespace Persistence {
    // Migrate to ArduinoJson 7 API (DynamicJsonDocument deprecated)
    extern JsonDocument _document;
    extern Bytes _buffer;
}}

// src/Utilities/Persistence.cpp - BEFORE (line 7)
/*static*/ DynamicJsonDocument _document(Type::Persistence::DOCUMENT_MAXSIZE);
/*static*/ Bytes _buffer(Type::Persistence::BUFFER_MAXSIZE);

// src/Utilities/Persistence.cpp - AFTER
namespace RNS { namespace Persistence {
    JsonDocument _document;  // Single definition - v7 uses elastic allocation
    Bytes _buffer(Type::Persistence::BUFFER_MAXSIZE);
}}
```

### MEM-02: Pre-allocate ResourceData Vectors

```cpp
// src/ResourceData.h - Add constant near class definition
class ResourceData {
public:
    // Maximum parts = MAX_EFFICIENT_SIZE / minimum SDU
    // MAX_EFFICIENT_SIZE = 16384, minimum SDU ~64 = ~256 parts max
    static constexpr size_t MAX_PARTS = 256;

    ResourceData(const Link& link) : _link(link) {
        _parts.reserve(MAX_PARTS);
        _hashmap.reserve(MAX_PARTS);
    }
    // ...
};
```

### CONC-01: Enable Task Watchdog Timer

```
# examples/lxmf_tdeck/sdkconfig.defaults - Add after boot optimizations section

# ============================================================================
# Task Watchdog Timer (TWDT)
# Detects task starvation and deadlock conditions
# ============================================================================
CONFIG_ESP_TASK_WDT_EN=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y
```

```cpp
// src/UI/LVGL/LVGLInit.cpp - Add to LVGL task

#include "esp_task_wdt.h"

void LVGLInit::lvgl_task(void* param) {
    LVGLInit* self = static_cast<LVGLInit*>(param);

    // Subscribe this task to TWDT
    esp_task_wdt_add(nullptr);  // nullptr = current task

    while (true) {
        if (xSemaphoreTakeRecursive(self->_mutex, portMAX_DELAY) == pdTRUE) {
            lv_task_handler();
            xSemaphoreGiveRecursive(self->_mutex);
        }
        esp_task_wdt_reset();  // Feed watchdog
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// examples/common/ble_interface/BLEInterface.cpp - Add to BLE task

void BLEInterface::ble_task(void* param) {
    BLEInterface* self = static_cast<BLEInterface*>(param);

    // Subscribe this task to TWDT
    esp_task_wdt_add(nullptr);

    while (true) {
        self->loop();
        esp_task_wdt_reset();  // Feed watchdog
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### CONC-02: Fix LXStamper Yield Frequency

```cpp
// src/LXMF/LXStamper.cpp - Modify generate_stamp loop (around line 195)

// BEFORE:
#ifdef ESP_PLATFORM
    if (rounds % 100 == 0) {
        vTaskDelay(1);
    }
#endif

// AFTER:
#ifdef ESP_PLATFORM
    if (rounds % 10 == 0) {
        vTaskDelay(1);        // Yield for 1 tick (more frequent)
        esp_task_wdt_reset(); // Feed watchdog during long operations
    }
#endif
```

### CONC-03: Add Mutex Protection to BLE Pending Queues

```cpp
// examples/common/ble_interface/BLEInterface.cpp

void BLEInterface::onHandshakeComplete(const Bytes& mac, const Bytes& identity, bool is_central) {
    // ADDED: Lock before modifying queue
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    if (_pending_handshakes.size() >= MAX_PENDING_HANDSHAKES) {
        WARNING("BLEInterface: Pending handshake queue full, dropping handshake");
        return;
    }
    PendingHandshake pending;
    pending.mac = mac;
    pending.identity = identity;
    pending.is_central = is_central;
    _pending_handshakes.push_back(pending);
    DEBUG("BLEInterface::onHandshakeComplete: Queued handshake for deferred processing");
}

// Around line 866 - handleIncomingData deferred path
void BLEInterface::handleIncomingDataDeferred(const Bytes& identity, const Bytes& data) {
    // ... existing validation ...

    // ADDED: Lock before modifying queue
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    if (_pending_data.size() >= MAX_PENDING_DATA) {
        WARNING("BLEInterface: Pending data queue full, dropping data");
        return;
    }
    PendingData pending;
    pending.identity = identity;
    pending.data = data;
    _pending_data.push_back(pending);
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| DynamicJsonDocument | JsonDocument | ArduinoJson 7.0 | Elastic allocation, simpler API |
| Static in header | extern decl + cpp def | Always (C++ standard) | ODR compliance |
| Manual watchdog reset | esp_task_wdt API | ESP-IDF 4.0+ | Cleaner API, timer support |

**Deprecated/outdated:**
- `DynamicJsonDocument`: Replaced by `JsonDocument` in ArduinoJson 7
- `StaticJsonDocument<N>`: Also replaced by `JsonDocument`
- `CONFIG_TASK_WDT_*`: Renamed to `CONFIG_ESP_TASK_WDT_*` in ESP-IDF 5.x

## Open Questions

No significant open questions - all 5 fixes use well-documented, standard patterns.

1. **Vector reserve vs fixed pool for ResourceData**
   - What we know: MAX_PARTS ~256 is a reasonable upper bound
   - What's unclear: Whether reserve() is sufficient or full pool refactor needed
   - Recommendation: Start with reserve() - it's lowest effort and matches backlog recommendation

## Sources

### Primary (HIGH confidence)

- ESP-IDF 5.x documentation (Context7 /websites/espressif_projects_esp-idf_en_release-v5_5_esp32s3)
  - Task Watchdog Timer API: esp_task_wdt_init, esp_task_wdt_add, esp_task_wdt_reset
  - sdkconfig options: CONFIG_ESP_TASK_WDT_EN, CONFIG_ESP_TASK_WDT_TIMEOUT_S
- ArduinoJson documentation (Context7 /bblanchon/arduinojson)
  - JsonDocument (v7 API) replaces DynamicJsonDocument/StaticJsonDocument
- Codebase analysis (direct inspection)
  - Persistence.h:475, Persistence.cpp:7 - duplicate static definition confirmed
  - ResourceData.h:59,68 - _parts and _hashmap are std::vector
  - LXStamper.cpp:195 - yield every 100 rounds
  - BLEInterface.cpp:644,866 - callbacks modify queues without lock

### Secondary (MEDIUM confidence)

- Phase 3/4 audit findings (.planning/phases/03-*/03-RESEARCH.md, .planning/phases/04-*/04-RESEARCH.md)
  - Detailed location and severity of each issue
- Phase 5 backlog synthesis (.planning/phases/05-synthesis/BACKLOG.md)
  - WSJF scoring and prioritization rationale

### Tertiary (LOW confidence)

None - all findings verified against primary sources.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - All dependencies already in project
- Architecture: HIGH - Patterns verified against existing codebase
- Pitfalls: HIGH - Based on documented ESP32/FreeRTOS behaviors

**Research date:** 2026-01-24
**Valid until:** 2026-02-24 (30 days - stable ESP-IDF platform)
