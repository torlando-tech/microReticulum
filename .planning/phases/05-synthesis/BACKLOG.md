# microReticulum Stability Backlog

---
type: backlog
version: 1.0
generated: 2026-01-24
source_phases: [3, 4]
issue_count: 30
severity_distribution:
  critical: 0
  high: 9
  medium: 13
  low: 8
---

## Executive Summary

This backlog consolidates **30 issues** identified during Phases 3-4 stability audits: 13 memory allocation issues and 17 concurrency issues. The distribution is 0 Critical, 9 High, 13 Medium, and 8 Low severity. **Top priority (P1)** includes enabling Task Watchdog Timer (TWDT) and fixing thread-unsafe BLE pending queues - both high-impact with low effort. Recommended sprint allocation: P1 issues in Sprint 1 (1-2 days), P2 issues in Sprints 1-2 (5-7 days), P3 issues opportunistically.

**Note:** Phase 4 summary claimed 18 issues but documented 17 unique issues in the detail table. This backlog reflects the 17 documented issues.

---

## Priority Summary

| Priority | Count | WSJF Range | Effort Range | Sprint Recommendation |
|----------|-------|------------|--------------|----------------------|
| P1 | 5 | >= 3.0 | Low | Sprint 1 (immediate) |
| P2 | 8 | 1.5 - 2.99 | Low-Medium | Sprint 1-2 |
| P3 | 11 | 0.5 - 1.49 | Low-High | Sprint 2-3 (opportunistic) |
| P4 | 6 | < 0.5 | Low-High | Backlog (defer) |

---

## Issue Catalog

Sorted by WSJF score descending (highest priority first).

| ID | Source | Severity | Effort | WSJF | Title | Priority |
|----|--------|----------|--------|------|-------|----------|
| CONC-H1 | Phase 4 | High (7) | Low (2) | 3.50 | TWDT not configured for application tasks | P1 |
| CONC-H2 | Phase 4 | High (7) | Low (2) | 3.50 | LXStamper CPU hogging | P1 |
| CONC-H3 | Phase 4 | High (7) | Low (2) | 3.50 | Pending queues not thread-safe | P1 |
| MEM-H5 | Phase 3 | High (7) | Low (2) | 3.50 | Resource vectors resize during transfers | P1 |
| MEM-M4 | Phase 3 | Medium (4) | Trivial (1) | 4.00 | Duplicate static definition | P1 |
| MEM-M1 | Phase 3 | Medium (4) | Low (2) | 2.00 | Bytes newData make_shared pattern | P2 |
| MEM-M2 | Phase 3 | Medium (4) | Low (2) | 2.00 | PacketReceipt default constructor allocates | P2 |
| MEM-M3 | Phase 3 | Medium (4) | Low (2) | 2.00 | DynamicJsonDocument in Persistence | P2 |
| CONC-M1 | Phase 4 | Medium (4) | Low (2) | 2.00 | SettingsScreen missing LVGL_LOCK | P2 |
| CONC-M2 | Phase 4 | Medium (4) | Low (2) | 2.00 | ComposeScreen missing LVGL_LOCK | P2 |
| CONC-M3 | Phase 4 | Medium (4) | Low (2) | 2.00 | AnnounceListScreen missing LVGL_LOCK | P2 |
| CONC-M5 | Phase 4 | Medium (4) | Low (2) | 2.00 | Mutex timeout may lose cache updates | P2 |
| CONC-M6 | Phase 4 | Medium (4) | Low (2) | 2.00 | Discovered devices cache unbounded | P2 |
| MEM-H1 | Phase 3 | High (7) | Medium (5) | 1.40 | Bytes COW copy allocation | P3 |
| MEM-H3 | Phase 3 | High (7) | Medium (5) | 1.40 | Packet 9 Bytes members overhead | P3 |
| CONC-H4 | Phase 4 | High (7) | Medium (5) | 1.40 | Shutdown during active operations | P3 |
| MEM-L1 | Phase 3 | Low (1) | Trivial (1) | 1.00 | toHex string reallocation | P3 |
| CONC-L1 | Phase 4 | Low (1) | Trivial (1) | 1.00 | Native GAP handler volatile usage | P3 |
| CONC-L2 | Phase 4 | Low (1) | Trivial (1) | 1.00 | Undocumented 50ms delay | P3 |
| MEM-H4 | Phase 3 | High (7) | Medium (5) | 1.40 | PacketReceipt allocation | P3 |
| CONC-M7 | Phase 4 | Medium (4) | Low (2) | 2.00 | LVGL mutex uses portMAX_DELAY | P2 |
| CONC-M8 | Phase 4 | Medium (4) | Low (2) | 2.00 | Audio I2S blocking write | P2 |
| CONC-M9 | Phase 4 | Medium (4) | Low (2) | 2.00 | No formal mutex ordering enforcement | P2 |
| MEM-H2 | Phase 3 | High (7) | High (8) | 0.88 | Packet Object allocation | P3 |
| CONC-M4 | Phase 4 | Medium (4) | Medium (5) | 0.80 | Soft reset does not release NimBLE state | P3 |
| MEM-L2 | Phase 3 | Low (1) | Low (2) | 0.50 | BLEFragmenter temporary vector | P3 |
| MEM-L3 | Phase 3 | Low (1) | Low (2) | 0.50 | Fixed document size may be suboptimal | P3 |
| MEM-L4 | Phase 3 | Low (1) | Low (2) | 0.50 | std::map in ChannelData | P3 |
| CONC-L4 | Phase 4 | Low (1) | Low (2) | 0.50 | portMAX_DELAY masks deadlock detection | P3 |
| CONC-L3 | Phase 4 | Low (1) | Medium (5) | 0.20 | Link watchdog TODO not implemented | P4 |

---

## Priority 1 - Fix Immediately (WSJF >= 3.0)

High-impact, low-effort issues that should be addressed in Sprint 1.

| ID | Issue | Effort | Source |
|----|-------|--------|--------|
| MEM-M4 | Duplicate static definition in Persistence | Trivial | Phase 3 |
| CONC-H1 | TWDT not configured for application tasks | Low | Phase 4 |
| CONC-H2 | LXStamper CPU hogging (yields every 100 rounds) | Low | Phase 4 |
| CONC-H3 | Pending queues not thread-safe | Low | Phase 4 |
| MEM-H5 | Resource vectors resize during transfers | Low | Phase 3 |

**Sprint 1 effort estimate:** 2-3 days

---

## Priority 2 - Fix Before Production (WSJF 1.5-2.99)

Important issues with good effort/impact ratio.

| ID | Issue | Effort | Source |
|----|-------|--------|--------|
| MEM-M1 | Bytes newData make_shared pattern | Low | Phase 3 |
| MEM-M2 | PacketReceipt default constructor allocates | Low | Phase 3 |
| MEM-M3 | DynamicJsonDocument in Persistence | Low | Phase 3 |
| CONC-M1 | SettingsScreen missing LVGL_LOCK | Low | Phase 4 |
| CONC-M2 | ComposeScreen missing LVGL_LOCK | Low | Phase 4 |
| CONC-M3 | AnnounceListScreen missing LVGL_LOCK | Low | Phase 4 |
| CONC-M5 | Mutex timeout may lose cache updates | Low | Phase 4 |
| CONC-M6 | Discovered devices cache unbounded | Low | Phase 4 |
| CONC-M7 | LVGL mutex uses portMAX_DELAY | Low | Phase 4 |
| CONC-M8 | Audio I2S blocking write | Low | Phase 4 |
| CONC-M9 | No formal mutex ordering enforcement | Low | Phase 4 |

**Sprint 1-2 effort estimate:** 4-5 days

---

## Priority 3 - Fix When Convenient (WSJF 0.5-1.49)

Medium-priority issues to address opportunistically.

| ID | Issue | Effort | Source |
|----|-------|--------|--------|
| MEM-H1 | Bytes COW copy allocation | Medium | Phase 3 |
| MEM-H2 | Packet Object allocation | High | Phase 3 |
| MEM-H3 | Packet 9 Bytes members overhead | Medium | Phase 3 |
| MEM-H4 | PacketReceipt allocation | Medium | Phase 3 |
| MEM-L1 | toHex string reallocation | Trivial | Phase 3 |
| MEM-L2 | BLEFragmenter temporary vector | Low | Phase 3 |
| MEM-L3 | Fixed document size may be suboptimal | Low | Phase 3 |
| MEM-L4 | std::map in ChannelData | Low | Phase 3 |
| CONC-H4 | Shutdown during active operations | Medium | Phase 4 |
| CONC-M4 | Soft reset does not release NimBLE state | Medium | Phase 4 |
| CONC-L1 | Native GAP handler volatile usage | Trivial | Phase 4 |
| CONC-L2 | Undocumented 50ms delay | Trivial | Phase 4 |
| CONC-L4 | portMAX_DELAY masks deadlock detection | Low | Phase 4 |

**Effort estimate:** 8-12 days (spread across multiple sprints)

---

## Priority 4 - Defer (WSJF < 0.5)

Low-priority issues to address when time permits.

| ID | Issue | Effort | Source |
|----|-------|--------|--------|
| CONC-L3 | Link watchdog TODO not implemented | Medium | Phase 4 |

**Note:** Only one issue falls into P4. Most low-severity issues have trivial/low effort, making them worth addressing.

---

## Issue Detail Cards

### MEM-H1: Bytes COW copy allocation

**Source:** Phase 3 Memory Audit (03-AUDIT.md)
**Severity:** High (7)
**Effort:** Medium (5)
**WSJF Score:** 1.40
**Priority:** P3

**Problem:**
Per-packet writes create heap fragmentation via exclusiveData() COW copies. Each write to a shared Bytes object allocates a new backing buffer.

**Location:**
- `src/Bytes.cpp:42` - exclusiveData() function

**Fix Recommendation:**
Implement Bytes pool or arena allocator for packet-lifetime allocations. Consider pre-allocated buffer pool for common sizes.

**Verification:**
- Fragmentation % stable during sustained packet processing
- No increase in per-packet allocation count

---

### MEM-H2: Packet Object allocation

**Source:** Phase 3 Memory Audit (03-AUDIT.md)
**Severity:** High (7)
**Effort:** High (8)
**WSJF Score:** 0.88
**Priority:** P3

**Problem:**
Each Packet creates a new Object allocation (~400 bytes). High-frequency operation causes fragmentation over time.

**Location:**
- `src/Packet.cpp:22` - `new Object(...)` per packet

**Fix Recommendation:**
Implement PacketObjectPool with 16-32 slots. Acquire on packet creation, release on destruction.

**Verification:**
- Object pool hit rate > 90% in steady state
- No heap growth during sustained packet processing

---

### MEM-H3: Packet 9 Bytes members overhead

**Source:** Phase 3 Memory Audit (03-AUDIT.md)
**Severity:** High (7)
**Effort:** Medium (5)
**WSJF Score:** 1.40
**Priority:** P3

**Problem:**
Each Packet has 9 Bytes members, each with shared_ptr control block overhead (~24 bytes each = ~216 bytes per packet).

**Location:**
- `src/Packet.h:280-328` - Bytes member declarations

**Fix Recommendation:**
Inline fixed-size buffers for small, predictable members: `_packet_hash`, `_destination_hash`, `_transport_id`, `_ratchet_id`.

**Verification:**
- Per-packet memory reduced by ~100-150 bytes
- No functional regression in hash/ID handling

---

### MEM-H4: PacketReceipt allocation

**Source:** Phase 3 Memory Audit (03-AUDIT.md)
**Severity:** High (7)
**Effort:** Medium (5)
**WSJF Score:** 1.40
**Priority:** P3

**Problem:**
PacketReceipt allocated per-packet when receipts are enabled. Creates allocation even for NONE receipt type.

**Location:**
- `src/Packet.cpp:812` - PacketReceipt creation

**Fix Recommendation:**
Lazy allocation - only allocate when receipt type is not NONE. Consider pool for active receipts.

**Verification:**
- No allocation when receipt_policy is NONE
- Receipt functionality unchanged

---

### MEM-H5: Resource vectors resize during transfers

**Source:** Phase 3 Memory Audit (03-AUDIT.md)
**Severity:** High (7)
**Effort:** Low (2)
**WSJF Score:** 3.50
**Priority:** P1

**Problem:**
`_parts` and `_hashmap` vectors in ResourceData grow during large file transfers, causing fragmentation.

**Location:**
- `src/ResourceData.h:59` - `_parts` vector
- `src/ResourceData.h:68` - `_hashmap` vector

**Fix Recommendation:**
Pre-allocate with maximum expected size based on MTU and max resource size:
```cpp
static constexpr size_t MAX_PARTS = 256;
_parts.reserve(MAX_PARTS);
_hashmap.reserve(MAX_PARTS);
```

**Verification:**
- No vector reallocations during resource transfers
- Memory stable during large file transfers

---

### MEM-M1: Bytes newData make_shared pattern

**Source:** Phase 3 Memory Audit (03-AUDIT.md)
**Severity:** Medium (4)
**Effort:** Low (2)
**WSJF Score:** 2.00
**Priority:** P2

**Problem:**
`newData()` uses `new Data()` + `shared_ptr<Data>(p)` pattern, causing two allocations instead of one.

**Location:**
- `src/Bytes.cpp:11` - newData() function

**Fix Recommendation:**
Convert to `std::make_shared<Data>()` for single allocation.

**Verification:**
- Allocation count reduced by 1 per newData() call

---

### MEM-M2: PacketReceipt default constructor allocates

**Source:** Phase 3 Memory Audit (03-AUDIT.md)
**Severity:** Medium (4)
**Effort:** Low (2)
**WSJF Score:** 2.00
**Priority:** P2

**Problem:**
Default PacketReceipt constructor allocates even for NONE receipt type.

**Location:**
- `src/Packet.h:51` - Default constructor

**Fix Recommendation:**
Defer allocation until receipt is actually used. Initialize with nullptr.

**Verification:**
- No allocation for default-constructed receipts

---

### MEM-M3: DynamicJsonDocument in Persistence

**Source:** Phase 3 Memory Audit (03-AUDIT.md)
**Severity:** Medium (4)
**Effort:** Low (2)
**WSJF Score:** 2.00
**Priority:** P2

**Problem:**
Uses deprecated ArduinoJson 6 `DynamicJsonDocument` API instead of v7 `JsonDocument`.

**Location:**
- `src/Utilities/Persistence.h:475` - Static document declaration

**Fix Recommendation:**
Migrate to `JsonDocument` (ArduinoJson 7 API):
```cpp
// Before: static DynamicJsonDocument _document(MAXSIZE);
// After: static JsonDocument _document;
```

**Verification:**
- No deprecation warnings
- Persistence functionality unchanged

---

### MEM-M4: Duplicate static definition

**Source:** Phase 3 Memory Audit (03-AUDIT.md)
**Severity:** Medium (4)
**Effort:** Trivial (1)
**WSJF Score:** 4.00
**Priority:** P1

**Problem:**
`DynamicJsonDocument` defined in both header and cpp file, potential ODR violation.

**Location:**
- `src/Utilities/Persistence.h:475`
- `src/Utilities/Persistence.cpp:7`

**Fix Recommendation:**
Single definition in cpp file, declare extern in header (or use inline).

**Verification:**
- Clean compile with no linker warnings
- Single definition in binary

---

### MEM-L1: toHex string reallocation

**Source:** Phase 3 Memory Audit (03-AUDIT.md)
**Severity:** Low (1)
**Effort:** Trivial (1)
**WSJF Score:** 1.00
**Priority:** P3

**Problem:**
`toHex()` builds string character by character, causing multiple reallocations for large Bytes.

**Location:**
- `src/Bytes.cpp:170` - toHex() function

**Fix Recommendation:**
Reserve space upfront: `hex.reserve(_data->size() * 2);`

**Verification:**
- Single allocation for hex string
- No functional change

---

### MEM-L2: BLEFragmenter temporary vector

**Source:** Phase 3 Memory Audit (03-AUDIT.md)
**Severity:** Low (1)
**Effort:** Low (2)
**WSJF Score:** 0.50
**Priority:** P3

**Problem:**
`fragment()` returns a vector of Bytes, allocating per call.

**Location:**
- `src/BLE/BLEFragmenter.cpp:38` - fragment() function

**Fix Recommendation:**
Use output parameter pattern to allow caller-provided buffer.

**Verification:**
- No new vector allocation in hot path

---

### MEM-L3: Fixed document size may be suboptimal

**Source:** Phase 3 Memory Audit (03-AUDIT.md)
**Severity:** Low (1)
**Effort:** Low (2)
**WSJF Score:** 0.50
**Priority:** P3

**Problem:**
DOCUMENT_MAXSIZE fixed at 8192 bytes may waste memory for small documents or limit large ones.

**Location:**
- `src/Type.h:18` - Size constant

**Fix Recommendation:**
After ArduinoJson 7 migration, consider elastic allocation or size-based selection.

**Verification:**
- Memory usage appropriate for document size
- No truncation of large documents

---

### MEM-L4: std::map in ChannelData

**Source:** Phase 3 Memory Audit (03-AUDIT.md)
**Severity:** Low (1)
**Effort:** Low (2)
**WSJF Score:** 0.50
**Priority:** P3

**Problem:**
`std::map` allocates tree nodes per registration, minor fragmentation.

**Location:**
- `src/ChannelData.h:347`

**Fix Recommendation:**
Consider fixed array if registration count is bounded and small.

**Verification:**
- No tree node allocations if converted
- Functionality unchanged

---

### CONC-H1: TWDT not configured for application tasks

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, TASK-01)
**Severity:** High (7)
**Effort:** Low (2)
**WSJF Score:** 3.50
**Priority:** P1

**Problem:**
Task Watchdog Timer (TWDT) not configured for application tasks. Task starvation or deadlock goes undetected, system hangs requiring manual reset.

**Location:**
- `sdkconfig.defaults` - Missing TWDT configuration
- Task creation sites - No `esp_task_wdt_add()` calls

**Fix Recommendation:**
1. Enable in sdkconfig.defaults:
```
CONFIG_ESP_TASK_WDT=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y
```
2. Subscribe critical tasks:
```cpp
esp_task_wdt_add(_task_handle);
esp_task_wdt_reset(); // In task loop
```

**Verification:**
- TWDT triggers on simulated deadlock
- Normal operation unaffected

---

### CONC-H2: LXStamper CPU hogging

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, TASK-02)
**Severity:** High (7)
**Effort:** Low (2)
**WSJF Score:** 3.50
**Priority:** P1

**Problem:**
LXStamper yields only every 100 rounds during stamp generation (can take minutes), causing UI freeze and potential watchdog triggers.

**Location:**
- LXStamper stamp generation loop

**Fix Recommendation:**
Increase yield frequency to every 10 rounds:
```cpp
if (rounds % 10 == 0) {  // Was 100
    vTaskDelay(1);
    esp_task_wdt_reset();
}
```

**Verification:**
- UI responsive during stamp generation
- Stamp generation completes correctly

---

### CONC-H3: Pending queues not thread-safe

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, NIMBLE-01)
**Severity:** High (7)
**Effort:** Low (2)
**WSJF Score:** 3.50
**Priority:** P1

**Problem:**
`_pending_handshakes` and `_pending_data` queues accessed from NimBLE callbacks without mutex protection. Potential data corruption if callback and loop access simultaneously.

**Location:**
- BLEInterface callback handlers

**Fix Recommendation:**
Add lock_guard in callbacks:
```cpp
void BLEInterface::onHandshakeComplete(...) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _pending_handshakes.push_back(pending);
}
```

**Verification:**
- No crashes during concurrent BLE operations
- Data integrity maintained

---

### CONC-H4: Shutdown during active operations

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, NIMBLE-02)
**Severity:** High (7)
**Effort:** Medium (5)
**WSJF Score:** 1.40
**Priority:** P3

**Problem:**
BLE shutdown may occur during active operations, causing use-after-free if callbacks fire during cleanup.

**Location:**
- BLEInterface shutdown/restart logic

**Fix Recommendation:**
Add shutdown state tracking and wait for active operations to complete before cleanup.

**Verification:**
- No crashes during BLE restart
- Clean shutdown sequence

---

### CONC-M1: SettingsScreen missing LVGL_LOCK

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, LVGL-01)
**Severity:** Medium (4)
**Effort:** Low (2)
**WSJF Score:** 2.00
**Priority:** P2

**Problem:**
SettingsScreen constructor/destructor missing LVGL_LOCK for thread-safe widget creation/deletion.

**Location:**
- `src/UI/LXMF/SettingsScreen.cpp` - Constructor/destructor

**Fix Recommendation:**
Add LVGL_LOCK() wrapper in constructor and destructor.

**Verification:**
- No race conditions during screen transitions
- LVGL widget operations properly serialized

---

### CONC-M2: ComposeScreen missing LVGL_LOCK

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, LVGL-02)
**Severity:** Medium (4)
**Effort:** Low (2)
**WSJF Score:** 2.00
**Priority:** P2

**Problem:**
ComposeScreen constructor/destructor missing LVGL_LOCK for thread-safe widget creation/deletion.

**Location:**
- `src/UI/LXMF/ComposeScreen.cpp` - Constructor/destructor

**Fix Recommendation:**
Add LVGL_LOCK() wrapper in constructor and destructor.

**Verification:**
- No race conditions during screen transitions
- LVGL widget operations properly serialized

---

### CONC-M3: AnnounceListScreen missing LVGL_LOCK

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, LVGL-03)
**Severity:** Medium (4)
**Effort:** Low (2)
**WSJF Score:** 2.00
**Priority:** P2

**Problem:**
AnnounceListScreen constructor/destructor missing LVGL_LOCK for thread-safe widget creation/deletion.

**Location:**
- `src/UI/LXMF/AnnounceListScreen.cpp` - Constructor/destructor

**Fix Recommendation:**
Add LVGL_LOCK() wrapper in constructor and destructor.

**Verification:**
- No race conditions during screen transitions
- LVGL widget operations properly serialized

---

### CONC-M4: Soft reset does not release NimBLE state

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, NIMBLE-03)
**Severity:** Medium (4)
**Effort:** Medium (5)
**WSJF Score:** 0.80
**Priority:** P3

**Problem:**
Soft reset may not fully release NimBLE internal state, potentially causing resource leaks.

**Location:**
- NimBLE platform soft reset logic

**Fix Recommendation:**
Consider hard recovery with full deinit/init cycle for error recovery.

**Verification:**
- NimBLE state fully reset after soft reset
- No resource accumulation over repeated resets

---

### CONC-M5: Mutex timeout may lose cache updates

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, NIMBLE-04)
**Severity:** Medium (4)
**Effort:** Low (2)
**WSJF Score:** 2.00
**Priority:** P2

**Problem:**
Connection mutex timeout (100ms) silently fails, potentially losing cache updates.

**Location:**
- NimBLE connection mutex acquisition

**Fix Recommendation:**
Add logging for mutex timeout failures to aid debugging.

**Verification:**
- Timeout events logged
- Cache consistency maintained

---

### CONC-M6: Discovered devices cache unbounded

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, NIMBLE-05)
**Severity:** Medium (4)
**Effort:** Low (2)
**WSJF Score:** 2.00
**Priority:** P2

**Problem:**
Discovered devices cache may grow unbounded and potentially evict connected devices.

**Location:**
- BLE device discovery cache

**Fix Recommendation:**
Check connection status before cache eviction. Implement bounded cache with LRU eviction for non-connected devices.

**Verification:**
- Connected devices never evicted
- Cache size bounded

---

### CONC-M7: LVGL mutex uses portMAX_DELAY

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, TASK-03)
**Severity:** Medium (4)
**Effort:** Low (2)
**WSJF Score:** 2.00
**Priority:** P2

**Problem:**
LVGL mutex acquisition uses `portMAX_DELAY` (infinite wait), masking potential deadlocks.

**Location:**
- LVGL mutex acquisition sites

**Fix Recommendation:**
Add timeout variant for debug builds:
```cpp
#ifdef DEBUG
if (xSemaphoreTakeRecursive(_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    ERROR("Mutex timeout - possible deadlock");
}
#endif
```

**Verification:**
- Deadlock detection in debug builds
- Production behavior unchanged

---

### CONC-M8: Audio I2S blocking write

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, TASK-04)
**Severity:** Medium (4)
**Effort:** Low (2)
**WSJF Score:** 2.00
**Priority:** P2

**Problem:**
Audio I2S write uses `portMAX_DELAY`, blocking indefinitely if I2S buffer full.

**Location:**
- I2S audio write calls

**Fix Recommendation:**
Add reasonable timeout to I2S writes.

**Verification:**
- I2S writes don't block indefinitely
- Audio quality maintained

---

### CONC-M9: No formal mutex ordering enforcement

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, MUTEX-01)
**Severity:** Medium (4)
**Effort:** Low (2)
**WSJF Score:** 2.00
**Priority:** P2

**Problem:**
Mutex ordering is implicit via code structure. No enforcement mechanism to prevent future violations.

**Location:**
- Cross-subsystem mutex usage

**Fix Recommendation:**
Add debug-mode mutex ordering assertions or static analysis rules. Document ordering in CONTRIBUTING.md.

**Verification:**
- Ordering violations detected in debug builds
- Documentation updated

---

### CONC-L1: Native GAP handler volatile usage

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, NIMBLE-06)
**Severity:** Low (1)
**Effort:** Trivial (1)
**WSJF Score:** 1.00
**Priority:** P3

**Problem:**
Native GAP handler uses `volatile` for complex state, which doesn't provide atomicity guarantees.

**Location:**
- Native GAP handler state variables

**Fix Recommendation:**
Document the risk and rationale. Consider atomic types if state needs true atomicity.

**Verification:**
- Documentation added
- No functional change unless atomicity needed

---

### CONC-L2: Undocumented 50ms delay

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, NIMBLE-07)
**Severity:** Low (1)
**Effort:** Trivial (1)
**WSJF Score:** 1.00
**Priority:** P3

**Problem:**
50ms delay in error recovery path is undocumented, unclear why this specific value.

**Location:**
- BLE error recovery path

**Fix Recommendation:**
Add documentation explaining the delay rationale (e.g., NimBLE stack recovery time).

**Verification:**
- Comment added explaining delay purpose

---

### CONC-L3: Link watchdog TODO not implemented

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, TASK-05)
**Severity:** Low (1)
**Effort:** Medium (5)
**WSJF Score:** 0.20
**Priority:** P4

**Problem:**
Link watchdog is marked TODO but not implemented. Links may hang without detection.

**Location:**
- Link management code

**Fix Recommendation:**
Implement Link watchdog using FreeRTOS timer when needed.

**Verification:**
- Hung links detected and handled
- No false positives on slow links

---

### CONC-L4: portMAX_DELAY masks deadlock detection

**Source:** Phase 4 Concurrency Audit (04-SUMMARY.md, MUTEX-02)
**Severity:** Low (1)
**Effort:** Low (2)
**WSJF Score:** 0.50
**Priority:** P3

**Problem:**
Multiple sites use `portMAX_DELAY` for mutex acquisition, masking deadlock conditions.

**Location:**
- Various mutex acquisition sites

**Fix Recommendation:**
Add debug timeout to detect stuck tasks. Production can keep infinite wait for stability.

**Verification:**
- Deadlocks detected in debug builds
- Production stability maintained

---

## Appendix: Source Reference

| Source Document | Issues Extracted |
|-----------------|------------------|
| `.planning/phases/03-memory-allocation-audit/03-AUDIT.md` | MEM-H1 through MEM-L4 (13 issues) |
| `.planning/phases/04-concurrency-audit/04-SUMMARY.md` | CONC-H1 through CONC-L4 (17 issues) |

**Total Issues:** 30

---

*Generated: 2026-01-24*
*Phase 5: Synthesis*
*microReticulum Stability Audit Milestone Complete*
