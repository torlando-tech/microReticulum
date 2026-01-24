# Phase 3 Plan 03: ArduinoJson and Persistence Audit Findings

## Executive Summary

ArduinoJson version 7.4.2 is configured in platformio.ini. The audit found **one deprecated pattern** requiring migration: `DynamicJsonDocument` usage in Persistence.h/cpp. The MessageStore implementation demonstrates the correct ArduinoJson 7 pattern with document reuse. UI allocations are startup-only, and BLE subsystems use fixed-size pools as expected.

**Key Findings:**
- 1 deprecated ArduinoJson 6 API usage (DynamicJsonDocument in Persistence)
- 1 correct ArduinoJson 7 pattern (JsonDocument in MessageStore)
- UI screen allocations are startup-only (8 screen objects)
- BLE subsystems use fixed-size pools throughout
- LVGL buffers correctly allocated in PSRAM (307KB)

## MEM-05: ArduinoJson Usage Audit

### ArduinoJson Version

- **Version in platformio.ini:** 7.4.2
- **API compatibility:** Mixed - one deprecated v6 pattern found

### Usage Sites

| File | Line | Pattern | Document Type | Size | Reused? | Severity |
|------|------|---------|---------------|------|---------|----------|
| MessageStore.h | 367 | `JsonDocument _json_doc` | Member variable | Elastic (v7) | Yes | N/A (correct) |
| Persistence.h | 475 | `static DynamicJsonDocument` | Static global | 8192 bytes | Yes | Medium |
| Persistence.cpp | 7 | `DynamicJsonDocument _document` | Static global | 8192 bytes | Yes | Medium |

### Deprecated Patterns

| File | Line | Old Pattern | Recommended | Severity |
|------|------|-------------|-------------|----------|
| Persistence.h | 475 | `DynamicJsonDocument _document(DOCUMENT_MAXSIZE)` | `JsonDocument _document` | Medium |
| Persistence.cpp | 7 | `DynamicJsonDocument _document(DOCUMENT_MAXSIZE)` | `JsonDocument _document` | Medium |

**Note:** Both `.h` and `.cpp` declare the same static variable. This is a C++ ODR (One Definition Rule) concern but works due to `static` keyword. After migration, only one definition should exist.

### Document Reuse Analysis

**Good Pattern (MessageStore):**
```cpp
// src/LXMF/MessageStore.h:367
JsonDocument _json_doc;  // Member variable, reused across operations
```

The MessageStore correctly:
1. Declares JsonDocument as class member (line 367)
2. Calls `_json_doc.clear()` before each use (lines 131, 197, 256, 354, 406, 454)
3. Never creates temporary documents

**Legacy Pattern (Persistence):**
```cpp
// src/Utilities/Persistence.h:475
static DynamicJsonDocument _document(Type::Persistence::DOCUMENT_MAXSIZE);
```

The Persistence layer:
1. Uses deprecated `DynamicJsonDocument` API
2. Fixed capacity of 8192 bytes (may limit or waste memory)
3. Is reused correctly via static declaration

### Size Analysis

| Constant | Value | Location | Notes |
|----------|-------|----------|-------|
| DOCUMENT_MAXSIZE | 8192 bytes | Type.h:18 | Main JSON document size |
| BUFFER_MAXSIZE | 12288 bytes | Type.h:20 | 1.5x document size for serialization |

**Size Appropriateness:**
- 8KB is reasonable for destination table entries and message metadata
- ArduinoJson 7's elastic allocation would handle variable sizes better
- Current fixed size may waste memory for small documents or fail for large ones

### deserializeJson/serializeJson Call Sites

| File | Line | Function | Document Used |
|------|------|----------|---------------|
| MessageStore.cpp | 132 | deserializeJson | _json_doc (member) |
| MessageStore.cpp | 228 | serializeJsonPretty | _json_doc (member) |
| MessageStore.cpp | 276 | serializeJsonPretty | _json_doc (member) |
| MessageStore.cpp | 355 | deserializeJson | _json_doc (member) |
| MessageStore.cpp | 407 | deserializeJson | _json_doc (member) |
| MessageStore.cpp | 455 | deserializeJson | _json_doc (member) |
| MessageStore.cpp | 466 | serializeJson | _json_doc (member) |
| Transport.cpp | 4559 | deserializeJson | Persistence::_document |
| Transport.cpp | 4709 | serializeJson | Persistence::_document |
| Persistence.h | 485 | serializeJson | _document (static) |
| Persistence.h | 501 | serializeJson | _document (static) |
| Persistence.h | 534 | deserializeJson | _document (static) |
| Persistence.h | 573 | serializeJson | _document (static) |
| Persistence.h | 614 | serializeJson | _document (static) |
| Persistence.h | 665 | deserializeJson | _document (static) |

All usage sites properly reuse their respective documents.

## UI Subsystem Summary

### Screen Object Allocations (Startup-Only)

| Screen Type | Allocation Line | Notes |
|-------------|-----------------|-------|
| ConversationListScreen | UIManager.cpp:76 | `new ConversationListScreen()` |
| ChatScreen | UIManager.cpp:77 | `new ChatScreen()` |
| ComposeScreen | UIManager.cpp:78 | `new ComposeScreen()` |
| AnnounceListScreen | UIManager.cpp:79 | `new AnnounceListScreen()` |
| StatusScreen | UIManager.cpp:80 | `new StatusScreen()` |
| QRScreen | UIManager.cpp:81 | `new QRScreen()` |
| SettingsScreen | UIManager.cpp:82 | `new SettingsScreen()` |
| PropagationNodesScreen | UIManager.cpp:83 | `new PropagationNodesScreen()` |

**Analysis:**
- All 8 screen objects allocated once at startup in `UIManager::init()`
- Objects persist for application lifetime (deleted in destructor)
- No per-frame or periodic UI allocations detected
- Pattern is correct: startup-only allocation with long lifetime

### LVGL Buffer Allocations (PSRAM)

From `src/Hardware/TDeck/Display.cpp`:
```cpp
// Line 32-37: LVGL buffers in PSRAM
lv_color_t* buf1 = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
lv_color_t* buf2 = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
```

- **Buffer size:** 320 x 240 x 2 bytes x 2 buffers = 307,200 bytes (~307KB)
- **Location:** PSRAM (MALLOC_CAP_SPIRAM)
- **Pattern:** Double-buffered display rendering

**Verdict:** LVGL allocations are correctly placed in PSRAM.

## BLE Subsystem Summary

### Pool-Based Architecture Confirmed

| Component | Pool Size | Fixed Array | Notes |
|-----------|-----------|-------------|-------|
| BLEReassembler | MAX_PENDING_REASSEMBLIES = 8 | `_pending_pool[8]` | Per-peer reassembly slots |
| BLEReassembler | MAX_FRAGMENTS_PER_REASSEMBLY = 32 | `fragments[32]` per slot | Fragment storage per reassembly |
| BLEPeerManager | PEERS_POOL_SIZE = 8 | `PeerByIdentitySlot` pool | Peer tracking |
| BLEPeerManager | MAC_IDENTITY_POOL_SIZE = 8 | MAC-to-identity mapping | Address translation |
| BLEPeerManager | MAX_CONN_HANDLES = 8 | Connection handle array | Platform mapping |
| BLEIdentityManager | ADDRESS_IDENTITY_POOL_SIZE = 16 | Address-identity pool | Identity handshake |
| BLEIdentityManager | HANDSHAKE_POOL_SIZE = 4 | Pending handshakes | In-progress handshakes |

### BLEFragmenter Allocation Pattern

| File | Line | Pattern | Frequency | Severity |
|------|------|---------|-----------|----------|
| BLEFragmenter.cpp | 38-39 | `std::vector<Bytes> fragments` | Per-packet | Low |

The `BLEFragmenter::fragment()` function returns a temporary `std::vector<Bytes>`:
```cpp
std::vector<Bytes> BLEFragmenter::fragment(const Bytes& data, uint16_t sequence_base) {
    std::vector<Bytes> fragments;
    // ...
    return fragments;
}
```

**Impact Assessment:**
- Called per-packet for fragmentation
- Vector typically contains 1-3 fragments (500-byte MTU, 23-byte BLE payload)
- Small allocation, short-lived
- Could be optimized but low priority

**Recommendation:** Consider passing output array as parameter in Phase 5 optimization, but this is low severity.

### std::map Usage in Platform Layer

| File | Pattern | Purpose |
|------|---------|---------|
| NimBLEPlatform.h:269 | `std::map<uint16_t, NimBLEClient*>` | Client tracking |
| NimBLEPlatform.h:272 | `std::map<uint16_t, ConnectionHandle>` | Connection mapping |
| NimBLEPlatform.h:276 | `std::map<std::string, NimBLEAdvertisedDevice>` | Device discovery cache |
| BluedroidPlatform.h:287 | `std::map<uint16_t, BluedroidConnection>` | Connection mapping |

**Analysis:**
- Platform layer maps are bounded by MAX_CONN_HANDLES (8 connections)
- Discovery cache cleared on scan completion
- Low fragmentation risk due to small, bounded sizes
- Not blocking - platform implementations are external dependencies

## Issues by Severity

### Medium

| ID | Issue | File | Line | Recommendation |
|----|-------|------|------|----------------|
| AJ-01 | Deprecated DynamicJsonDocument API | Persistence.h | 475 | Migrate to JsonDocument |
| AJ-02 | Duplicate static definition | Persistence.h/cpp | 475/7 | Consolidate to single definition |

### Low

| ID | Issue | File | Line | Recommendation |
|----|-------|------|------|----------------|
| AJ-03 | Fixed document size may be suboptimal | Type.h | 18 | Consider elastic capacity after migration |
| BLE-01 | Temporary vector in fragment() | BLEFragmenter.cpp | 38 | Consider output parameter pattern |

## ArduinoJson 7 Migration Checklist

Migration from ArduinoJson 6 to 7 API:

- [ ] **Persistence.h line 475:** Change `static DynamicJsonDocument _document(Type::Persistence::DOCUMENT_MAXSIZE)` to `static JsonDocument _document`
- [ ] **Persistence.cpp line 7:** Remove duplicate definition (keep only header declaration or move to .cpp)
- [ ] **Verify compilation:** ArduinoJson 7's JsonDocument has elastic capacity
- [ ] **Test persistence operations:** Ensure save/load still work correctly
- [ ] **Consider DOCUMENT_MAXSIZE:** May no longer be needed with elastic documents

### Migration Code Change

**Before (v6):**
```cpp
// Persistence.h
static DynamicJsonDocument _document(Type::Persistence::DOCUMENT_MAXSIZE);

// Persistence.cpp
/*static*/ DynamicJsonDocument _document(Type::Persistence::DOCUMENT_MAXSIZE);
```

**After (v7):**
```cpp
// Persistence.h
static JsonDocument _document;

// Persistence.cpp - remove duplicate, or:
// extern JsonDocument Persistence::_document; if needed
```

## Recommendations for Phase 5

### Priority 1: ArduinoJson Migration (Medium)
Migrate Persistence.h/cpp to ArduinoJson 7 API. Low effort, eliminates deprecation warnings, enables elastic memory usage.

### Priority 2: Clean Up Duplicate Static (Low)
Resolve the duplicate static variable definition between Persistence.h and Persistence.cpp.

### Priority 3: BLEFragmenter Optimization (Low, Optional)
Consider changing `fragment()` to take an output vector reference:
```cpp
void fragment(const Bytes& data, std::vector<Bytes>& out, uint16_t sequence_base = 0);
```
This would allow caller to reuse vector allocation.

---

*Audit completed: 2026-01-24*
*MEM-05 requirement satisfied*
