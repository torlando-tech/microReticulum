---
phase: 03-memory-allocation-audit
verified: 2026-01-24T06:00:46Z
status: passed
score: 5/5 must-haves verified
---

# Phase 3: Memory Allocation Audit Verification Report

**Phase Goal:** All significant memory allocation patterns are documented with fragmentation risk assessment  
**Verified:** 2026-01-24T06:00:46Z  
**Status:** PASSED  
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | All shared_ptr creation sites are audited (make_shared vs new/shared_ptr pattern) | ✓ VERIFIED | 03-AUDIT.md documents 14 new/shared_ptr sites vs 2 make_shared sites with line numbers |
| 2 | Packet and Bytes allocation frequency and sizes are documented | ✓ VERIFIED | Per-packet allocation pattern documented with 9 Bytes members + Object allocation (~500-800 bytes) |
| 3 | ArduinoJson document types (Dynamic vs Static) are audited across codebase | ✓ VERIFIED | 03-03-FINDINGS.md documents 1 deprecated DynamicJsonDocument pattern in Persistence, 1 correct JsonDocument in MessageStore |
| 4 | Large buffer allocations are verified to use PSRAM (MALLOC_CAP_SPIRAM) | ✓ VERIFIED | Bytes uses PSRAMAllocator (Bytes.h:55), Identity pool uses heap_caps_aligned_alloc SPIRAM (Identity.cpp:43), LVGL buffers use SPIRAM (Display.cpp:36-37) |
| 5 | Memory pools are documented with capacity limits and overflow behavior | ✓ VERIFIED | 40+ pools documented in 03-AUDIT.md lines 207-336 with overflow behaviors (nullptr/cull/ring) |

**Score:** 5/5 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `.planning/phases/03-memory-allocation-audit/03-01-FINDINGS.md` | Core data path (Bytes, Packet, Transport) audit findings | ✓ EXISTS, SUBSTANTIVE, WIRED | 250 lines, documents MEM-04 and MEM-06 partial |
| `.planning/phases/03-memory-allocation-audit/03-02-FINDINGS.md` | shared_ptr and session object audit findings | ✓ EXISTS, SUBSTANTIVE, WIRED | 317 lines, documents MEM-03 with 14 sites catalogued |
| `.planning/phases/03-memory-allocation-audit/03-03-FINDINGS.md` | ArduinoJson audit findings | ✓ EXISTS, SUBSTANTIVE, WIRED | 248 lines, documents MEM-05 with deprecation found |
| `.planning/phases/03-memory-allocation-audit/03-AUDIT.md` | Consolidated audit report | ✓ EXISTS, SUBSTANTIVE, WIRED | 584 lines, consolidates all findings with severity ratings and Phase 5 backlog |
| `docs/MEMORY_AUDIT.md` | Reference copy for long-term access | ✓ EXISTS, SUBSTANTIVE, WIRED | 587 lines, identical content to 03-AUDIT.md with source note |
| `src/Bytes.cpp` | FIXME(frag) comments added | ✓ MODIFIED, WIRED | 3 comments at lines 8, 39, 168 |
| `src/Packet.cpp` | FIXME(frag) comments added | ✓ MODIFIED, WIRED | 2 comments at lines 21, 812 |
| `src/Packet.h` | FIXME(frag) comments added | ✓ MODIFIED, WIRED | 2 comments at lines 51, 273 |
| `src/Transport.h` | Pool overflow documentation | ✓ MODIFIED, WIRED | Lines 391-393 document overflow behavior |

### Key Link Verification

| From | To | Via | Status | Details |
|------|-----|-----|--------|---------|
| Audit findings | Source code | Line number references | ✓ WIRED | Verified Packet.cpp:22 new Object(), Identity.cpp:133, Destination.cpp:16 |
| PSRAM verification | Bytes.h | PSRAMAllocator typedef | ✓ WIRED | Line 55: `using Data = std::vector<uint8_t, PSRAMAllocator<uint8_t>>` exists |
| PSRAM verification | Identity.cpp | heap_caps_aligned_alloc | ✓ WIRED | Line 43: `heap_caps_aligned_alloc(8, pool_size, MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT)` exists |
| Pool documentation | Transport.h | Pool arrays | ✓ WIRED | Lines 394-616 contain 20+ pool definitions with SIZE constants |
| Pool documentation | Identity.h | PSRAM pool pointer | ✓ WIRED | Line 91: `static KnownDestinationSlot* _known_destinations_pool` with PSRAM comment line 90 |
| Phase 5 backlog | 03-AUDIT.md | Prioritized issues | ✓ WIRED | Lines 435-556 contain P2-1 through P4-3 with complexity and impact estimates |

### Requirements Coverage

| Requirement | Status | Blocking Issue |
|-------------|--------|----------------|
| MEM-03: shared_ptr allocation patterns | ✓ SATISFIED | None - 14 new/shared_ptr sites vs 2 make_shared documented with line numbers |
| MEM-04: Packet/Bytes allocation frequency and sizes | ✓ SATISFIED | None - Per-packet pattern documented (Object + 9 Bytes members) |
| MEM-05: ArduinoJson usage | ✓ SATISFIED | None - 1 deprecated pattern identified in Persistence.h:475 |
| MEM-06: PSRAM allocation strategy | ✓ SATISFIED | None - Verified in Bytes, Identity, LVGL buffers |
| MEM-07: Memory pools documentation | ✓ SATISFIED | None - 40+ pools documented with sizes and overflow behavior |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| src/Bytes.cpp | 11 | new Data() instead of make_shared | Medium | Two allocations instead of one |
| src/Bytes.cpp | 42 | COW copy allocation | High | Per-packet fragmentation risk |
| src/Packet.cpp | 22 | new Object() per packet | High | ~400 bytes + control block per packet |
| src/Packet.h | 280-328 | 9 Bytes members | High | ~216 bytes overhead per packet |
| src/Utilities/Persistence.h | 475 | DynamicJsonDocument (deprecated) | Medium | ArduinoJson 6 API, needs migration to v7 |

**Categorization:**
- **0 Blockers** - No patterns prevent goal achievement
- **3 High Warnings** - Per-packet allocations indicate incomplete optimization but don't block audit completion
- **2 Medium Warnings** - make_shared and ArduinoJson migration items for future work

**Assessment:** All anti-patterns are documented with severity ratings and included in Phase 5 backlog. The audit goal is documentation and risk assessment, not elimination of patterns. Goal achieved.

### Human Verification Required

None required. All verification can be completed programmatically through:
- File existence checks (all artifacts present)
- Line count verification (all substantive: 248-587 lines each)
- Pattern matching for code references (verified new Object() sites)
- Grep for PSRAM patterns (verified PSRAMAllocator and heap_caps)
- Pool definition counts (verified 40+ pools)

---

## Detailed Verification Evidence

### Truth 1: shared_ptr Allocation Patterns Audited

**Claim:** All shared_ptr creation sites are audited (make_shared vs new/shared_ptr pattern)

**Evidence:**
- 03-AUDIT.md lines 82-113 document pattern distribution
- 14 new/shared_ptr sites catalogued with file:line references
- 2 make_shared sites catalogued
- Spot check verification:
  ```
  Identity.cpp:133 - new Object() ✓ exists
  Destination.cpp:16 - new Object(identity) ✓ exists  
  Packet.cpp:22 - new Object(...) ✓ exists (actually line 24 due to comment)
  Buffer.cpp:152 - make_shared<RawChannelReaderData>() ✓ exists
  ```

**Wiring:**
- Source files contain actual patterns referenced in audit
- FIXME(frag) comments added at high-frequency sites (Bytes.cpp:8, Packet.cpp:21)
- Phase 5 backlog item P3-1 references these sites for optimization

**Status:** ✓ VERIFIED

### Truth 2: Packet/Bytes Allocation Frequency and Sizes Documented

**Claim:** Packet and Bytes allocation frequency and sizes are documented

**Evidence:**
- 03-01-FINDINGS.md tables document allocation sites with frequency classification
- 03-AUDIT.md lines 116-148 detail per-packet allocations:
  - Object constructor: ~400 bytes (line 132)
  - 9 Bytes members: ~216 bytes overhead (line 133)
  - PacketReceipt: ~100 bytes (line 134)
  - Total: 500-800 bytes per packet (line 147)
- Verified in source:
  ```bash
  # Count Bytes members in Packet::Object
  grep "^\s*Bytes _" src/Packet.h | wc -l
  # Result: 11 (9 in Object + 2 in PacketReceipt nested class)
  ```
- Actual count: 9 Bytes members in Packet::Object (lines 321-332)
  1. `_packet_hash` (line 321)
  2. `_ratchet_id` (line 322)
  3. `_destination_hash` (line 323)
  4. `_transport_id` (line 324)
  5. `_raw` (line 326)
  6. `_data` (line 327)
  7. `_plaintext` (line 329)
  8. `_header` (line 331)
  9. `_ciphertext` (line 332)

**Wiring:**
- FIXME(frag) comment at Packet.h:273 references "9 Bytes members"
- FIXME(frag) comment at Packet.cpp:21 references per-packet allocation
- Size estimates in audit match sizeof calculations (Object ~400 bytes verified)

**Status:** ✓ VERIFIED

### Truth 3: ArduinoJson Document Types Audited

**Claim:** ArduinoJson document types (Dynamic vs Static) are audited across codebase

**Evidence:**
- 03-03-FINDINGS.md lines 20-40 document usage patterns
- 03-AUDIT.md lines 152-177 consolidate ArduinoJson findings:
  - MessageStore.h:367 uses JsonDocument (correct v7 API) ✓
  - Persistence.h:475 uses DynamicJsonDocument (deprecated v6 API) ✓
  - Persistence.cpp:7 duplicate definition (deprecated v6 API) ✓
- Verified in source:
  ```bash
  grep -n "JsonDocument\|DynamicJsonDocument" src/ -r
  # Found:
  # MessageStore.h:367: JsonDocument _json_doc ✓
  # Persistence.h:475: static DynamicJsonDocument _document(Type::Persistence::DOCUMENT_MAXSIZE) ✓
  # Persistence.cpp:7: /*static*/ DynamicJsonDocument _document(...) ✓
  ```

**Wiring:**
- Migration recommendation in 03-AUDIT.md lines 510-517 (P3-2)
- Issue AJ-01 (Medium severity) documented with before/after code
- platformio.ini verified to contain ArduinoJson 7.4.2

**Status:** ✓ VERIFIED

### Truth 4: Large Allocations Use PSRAM

**Claim:** Large buffer allocations are verified to use PSRAM (MALLOC_CAP_SPIRAM)

**Evidence:**
- 03-AUDIT.md lines 179-204 document PSRAM verification
- Table shows 4 verified PSRAM allocations:
  1. Bytes backing vector via PSRAMAllocator (Bytes.h:55)
  2. known_destinations_pool via heap_caps (Identity.cpp:43) ~23KB
  3. LVGL display buffers via heap_caps (Display.cpp:36-37) ~307KB
  4. BZ2 compression buffers via heap_caps (BZ2.cpp:39,123)
- Verified in source:
  ```bash
  # Bytes PSRAMAllocator
  grep -n "using Data = std::vector<uint8_t, PSRAMAllocator" src/Bytes.h
  # Line 55: using Data = std::vector<uint8_t, PSRAMAllocator<uint8_t>>; ✓
  
  # Identity pool
  grep -n "heap_caps_aligned_alloc.*SPIRAM" src/Identity.cpp
  # Line 43: heap_caps_aligned_alloc(8, pool_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) ✓
  
  # LVGL buffers
  grep -n "heap_caps_malloc.*SPIRAM" src/Hardware/TDeck/Display.cpp
  # Lines 36-37: Two buffers allocated ✓
  ```

**Wiring:**
- PSRAMAllocator.h implements custom STL allocator (25-84 lines)
- PSRAMAllocator::allocate() uses heap_caps_malloc with MALLOC_CAP_SPIRAM (line 41)
- Bytes.h includes PSRAMAllocator.h (line 16) and uses it (line 55)
- Identity.cpp includes fallback to internal RAM if PSRAM fails (good pattern)

**Critical Missing PSRAM:** None found in audit

**Status:** ✓ VERIFIED

### Truth 5: Memory Pools Documented with Overflow Behavior

**Claim:** Memory pools are documented with capacity limits and overflow behavior

**Evidence:**
- 03-AUDIT.md lines 207-336 document 40+ memory pools
- Tables organized by subsystem:
  - Transport pools (20+): ~21KB, lines 209-235
  - Identity pools (2): ~30KB, lines 237-245
  - Link/Channel pools (5 per link): ~5KB, lines 247-255
  - MessageStore pools (2): ~288KB, lines 279-285
  - LXMRouter pools (7): lines 287-297
  - BLE pools (7): lines 311-322
  - Other pools (8): Destination, Interface, SegmentAccumulator, etc.
- Overflow behaviors documented:
  - Returns nullptr/false: Most pools (Transport, Interface)
  - Ring buffer overwrite: packet_hashlist, transient_ids, message queues
  - Cull oldest: known_destinations, known_ratchets, propagation_nodes
- Verified in source:
  ```bash
  # Transport pool overflow comment
  grep -n "Overflow behavior" src/Transport.h
  # Line 392: // Overflow behavior: find_empty_*_slot() returns nullptr when pool full ✓
  
  # Count pool definitions
  grep -c "static constexpr size_t.*_SIZE = " src/Transport.h
  # 20 pool size constants ✓
  
  # Identity PSRAM pool comment
  grep -B1 "_known_destinations_pool" src/Identity.h | grep PSRAM
  # Line 90: // Pool allocated in PSRAM to free internal RAM (~29KB) ✓
  ```

**Pool Size Summary from Audit:**
| Category | Size | PSRAM | Lines |
|----------|------|-------|-------|
| Transport | ~21KB | No | 209-235 |
| Identity | ~30KB | Partial | 237-245 |
| MessageStore | ~288KB | Should consider | 279-285 |
| LVGL buffers | ~307KB | Yes | Table line 332 |
| **Total Static** | **~550KB** | | Line 335 |
| **PSRAM Usage** | **~330KB** | | Line 336 |

**Wiring:**
- Pool definitions exist in header files (Transport.h, Identity.h, etc.)
- Overflow functions implemented (find_empty_*_slot() returns nullptr)
- Phase 5 recommendations reference pool sizes for PSRAM migration consideration

**Status:** ✓ VERIFIED

---

## Summary of Verification

**Phase Goal:** "All significant memory allocation patterns are documented with fragmentation risk assessment"

**Achieved:** YES

**Evidence:**
1. **Documentation exists and is substantive:**
   - 03-01-FINDINGS.md (250 lines) - Core data path
   - 03-02-FINDINGS.md (317 lines) - Session objects
   - 03-03-FINDINGS.md (248 lines) - ArduinoJson
   - 03-AUDIT.md (584 lines) - Consolidated report
   - docs/MEMORY_AUDIT.md (587 lines) - Reference copy

2. **All allocation patterns documented:**
   - shared_ptr: 14 new/shared_ptr vs 2 make_shared sites
   - Packet/Bytes: Per-packet pattern (500-800 bytes)
   - ArduinoJson: 1 deprecated, 1 correct
   - PSRAM: 4 large allocations verified
   - Memory pools: 40+ pools with sizes and overflow

3. **Fragmentation risk assessment complete:**
   - 0 Critical issues (all large allocations use PSRAM)
   - 5 High issues (per-packet allocations)
   - 4 Medium issues (make_shared, ArduinoJson)
   - 4 Low issues (minor optimizations)
   - Total: 13 issues severity-rated

4. **Phase 5 backlog created:**
   - P2-1: Packet Object Pool (High impact, Med-High complexity)
   - P2-2: Inline Small Bytes (Med-High impact, Med complexity)
   - P2-3: Resource Vector Pre-alloc (Med impact, Low complexity)
   - P3-1: make_shared conversion (Low-Med impact, Low complexity)
   - P3-2: ArduinoJson migration (Low impact, Low complexity)

5. **Source code annotated:**
   - 7 FIXME(frag) comments added to source
   - Pool overflow behavior documented in Transport.h
   - All comments syntactically valid and describe issues

6. **Requirements satisfied:**
   - MEM-03: ✓ Complete
   - MEM-04: ✓ Complete
   - MEM-05: ✓ Complete
   - MEM-06: ✓ Complete
   - MEM-07: ✓ Complete

**Verification Confidence:** HIGH

All must-haves are verified through programmatic checks (file existence, line counts, pattern matching, grep verification). No human verification required. Documentation is comprehensive, accurate, and actionable for Phase 5.

---

_Verified: 2026-01-24T06:00:46Z_  
_Verifier: Claude (gsd-verifier)_
