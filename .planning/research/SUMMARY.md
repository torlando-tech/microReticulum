# Project Research Summary

**Project:** microReticulum Firmware Stability Audit
**Domain:** ESP32-S3 Embedded Firmware (FreeRTOS, BLE, LVGL)
**Researched:** 2026-01-23
**Confidence:** HIGH

## Executive Summary

This is a stability audit for existing ESP32-S3 firmware exhibiting two primary symptoms: slow boot times (15+ seconds) and crashes after extended runtime (hours/days). The firmware uses PlatformIO with Arduino framework, FreeRTOS for multi-tasking, NimBLE for wireless mesh, and LVGL for UI rendering on an 8MB PSRAM device. Expert analysis points to heap fragmentation as the most likely root cause of runtime crashes, caused by frequent small variable-size allocations from C++ shared_ptr patterns and ArduinoJson usage. Boot time issues are attributed to PSRAM memory testing, serial debug output, and potentially blocking initialization sequences.

The recommended approach combines runtime memory monitoring with systematic code audit. Priority one is instrumenting heap fragmentation metrics using ESP-IDF native APIs, then profiling boot sequences with esp_timer instrumentation. The architecture review reveals heavy reliance on shared_ptr for object lifetime management, which creates allocation patterns incompatible with long-running embedded systems. Migration to pool-based allocation for hot-path objects (Packet, Bytes) will eliminate fragmentation while preserving RAII safety.

Key risks include NimBLE's documented init/deinit memory leak (must initialize once and never restart), LVGL thread-safety violations from cross-task UI updates, and task stack overflows from crypto operations. All risks are mitigable through configuration changes (disable PSRAM memtest, enable stack canaries) and targeted code refactoring (pool allocators, mutex audits). The codebase already demonstrates awareness of best practices (LVGL mutex, stack monitoring), making remediation straightforward once root causes are confirmed.

## Key Findings

### Recommended Stack

**From STACK.md:** ESP-IDF provides comprehensive debugging capabilities accessible even from Arduino framework. The toolchain includes heap analysis APIs, FreeRTOS runtime statistics, and core dump functionality. Static analysis via PlatformIO check (cppcheck + clang-tidy) complements runtime monitoring.

**Core tools:**
- **ESP-IDF Heap APIs**: Runtime fragmentation detection via `heap_caps_get_largest_free_block()` vs `heap_caps_get_free_size()` — official Espressif API, highest confidence for fragmentation diagnosis
- **esp_timer instrumentation**: Microsecond-precision boot profiling — identifies slow initialization phases without external hardware
- **Core dumps to flash**: Post-mortem crash analysis — captures full backtrace and variable state even on unattended devices
- **JTAG debugging (ESP32-S3 built-in)**: Real-time debugging without external probe — SD card conflict limits production use but valuable for development
- **PlatformIO static analysis**: Automated detection of common errors — low overhead, run on every build

**Critical version notes:**
- ESP-IDF heap corruption detection: Enable CONFIG_HEAP_CORRUPTION_DETECTION=2 for comprehensive mode (detects use-after-free at 0xFEFEFEFE, uninitialized reads at 0xCECECECE)
- NimBLE memory leak: Known issue in init/deinit cycles — must initialize once at boot, never restart stack

### Expected Features (Stability Characteristics)

**From FEATURES.md:** These aren't features to build but stability practices to implement.

**Must have (table stakes):**
- Heap monitoring at runtime — detect fragmentation before crash (compare largest_block to total_free)
- Watchdog integration (TWDT) — detect task starvation/deadlock, feed watchdog from all tasks regularly
- Core dump to flash — post-mortem debugging, essential for field failures
- Fast flash mode (QIO @ 80MHz) — single biggest boot time improvement
- Bounded memory pools — prevent unbounded growth from network data, enforce collection limits

**Should have (advanced stability):**
- Pre-allocated buffers at startup — eliminate runtime fragmentation from large allocations
- Heap integrity checks — periodic `heap_caps_check_integrity()` catches corruption early
- Graceful degradation on low memory — disable non-essential features when heap drops below threshold
- Staged boot with splash screen — perceived responsiveness, initialize network in background
- Stack high water mark monitoring — right-size task stacks, already implemented in codebase

**Defer (already configured or low priority):**
- Heap tracing for leak detection — development tool, not production feature
- SystemView real-time tracing — valuable but requires setup, use if other tools insufficient
- Parallel initialization — complex synchronization, premature optimization

### Architecture Approach

**From ARCHITECTURE.md:** The codebase uses shared_ptr wrapper pattern for all major objects (Packet, Identity, Destination, Link). Public API classes wrap `std::shared_ptr<Implementation>` for automatic lifetime management. This provides safety and simplifies async operations but creates heap fragmentation from thousands of small allocations (object + control block). Current pools exist for tables but not for the underlying Packet/Bytes objects allocated in hot paths.

**Major components and patterns:**
1. **Memory allocation patterns** — Static allocation preferred, fixed-size pools for frequent objects, capability-based PSRAM routing for large buffers
2. **FreeRTOS task patterns** — Core pinning strategy (BLE/protocol on Core 0, LVGL/app on Core 1), conservative stack sizing with monitoring, yield-aware design with regular delays
3. **Object lifetime patterns** — shared_ptr wrapper (current), pool-backed smart pointers (recommended migration), RAII scoped locks (LVGLLock pattern is correct)
4. **PSRAM usage patterns** — Bulk storage in PSRAM (display buffers, message store), working memory in internal RAM (crypto, DMA, hot path data)

**Audit focus areas identified:**
- High priority: Packet allocation path, Bytes class allocations, BLE peer management, Transport destination table
- Medium priority: LVGL/BLE task synchronization, Link state machine, LXMF message routing
- Lower priority: Crypto buffer management, UI screen lifecycle

### Critical Pitfalls

**From PITFALLS.md — Top 7 most likely root causes:**

1. **Frequent small variable-size allocations (heap fragmentation)** — Repeated malloc/new for strings, buffers, shared_ptr control blocks creates "Swiss cheese" heap. Prevention: Pool allocation for Packet/Bytes, pre-allocate at boot, use StaticJsonDocument. Check: ArduinoJson, Bytes, String usage in loops.

2. **shared_ptr control block fragmentation** — Each shared_ptr creates 16-32 byte control block. Heavy usage scatters small allocations. Prevention: Always use make_shared (combines object + control block), consider intrusive reference counting for hot paths. Check: Audit all shared_ptr creation sites.

3. **PSRAM allocation strategy issues** — Internal RAM exhausted while PSRAM unused. Prevention: Explicit `heap_caps_malloc(MALLOC_CAP_SPIRAM)` for large buffers, DMA buffers require `MALLOC_CAP_INTERNAL`. Check: Verify large allocations use PSRAM, DMA uses internal.

4. **Serial debug output during initialization** — UART at 115200 baud transmits ~11.5KB/s, 1KB logging adds ~100ms. Prevention: Set log level to WARNING/ERROR, defer verbose logging. Check: Serial.printf in init paths, log level config.

5. **PSRAM memory test at boot** — Tests ~1 sec per 4MB, adds ~2 seconds for 8MB. Prevention: Disable CONFIG_SPIRAM_MEMTEST. Check: platformio.ini build flags, sdkconfig.

6. **Task stack overflow** — Insufficient stack corrupts heap. Prevention: Start with 8KB+, measure with uxTaskGetStackHighWaterMark(), enable CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK. Check: LVGL task (8KB adequate?), BLE task, crypto tasks.

7. **NimBLE init/deinit memory leak** — Known ESP-IDF issue, each cycle leaks memory. Prevention: Initialize once at boot, never deinit. Use advertising start/stop instead. Check: NimBLEPlatform.cpp for deinit calls.

## Implications for Roadmap

This is a stability audit of existing firmware, not a greenfield project. The roadmap should focus on systematic diagnosis, targeted fixes, and validation rather than new feature development.

### Phase 1: Memory Instrumentation & Baseline Measurement
**Rationale:** Cannot fix what you cannot measure. Establish runtime monitoring to confirm heap fragmentation hypothesis before making invasive code changes.
**Delivers:** Heap and stack monitoring dashboard, 24-hour baseline data showing fragmentation trends
**Addresses:** Table stakes monitoring (heap, stack, fragmentation detection)
**Avoids:** Pitfall 1, 2 (fragmentation detection), Pitfall 6 (stack overflow detection)
**Effort:** 2-3 days
**Tools:** ESP-IDF heap APIs, uxTaskGetStackHighWaterMark(), periodic logging to SD card or serial

### Phase 2: Boot Time Profiling & Quick Wins
**Rationale:** Boot time fixes are low-risk configuration changes with high user impact. Independent of memory fragmentation investigation.
**Delivers:** Boot time reduced from 15+ seconds to target <5 seconds
**Addresses:** Fast flash mode, reduced boot log, PSRAM test disable (all table stakes)
**Avoids:** Pitfall 4 (serial debug), Pitfall 5 (PSRAM test), Pitfall 3 (flash mode config)
**Effort:** 1-2 days
**Tools:** esp_timer instrumentation for per-section timing, platformio.ini configuration changes

### Phase 3: Critical Audit — Memory Allocation Patterns
**Rationale:** Based on Phase 1 data, audit code for fragmentation sources. Focus on Packet/Bytes hot paths and shared_ptr usage patterns identified in ARCHITECTURE.md.
**Delivers:** Documented allocation patterns, prioritized list of refactoring targets
**Addresses:** N/A (discovery phase)
**Avoids:** Pitfall 1 (variable allocations), Pitfall 2 (shared_ptr patterns)
**Effort:** 3-5 days
**Tools:** Code review, grep for new/malloc/make_shared, static analysis via pio check

### Phase 4: Critical Audit — Concurrency & NimBLE
**Rationale:** Parallel to Phase 3. Audit task synchronization, check for NimBLE leak, verify LVGL mutex usage.
**Delivers:** Documented thread-safety status, NimBLE init/deinit audit result
**Addresses:** Watchdog integration (verify all tasks yield), mutex usage (verify LVGL protection)
**Avoids:** Pitfall 7 (NimBLE leak), Pitfall 12 (deadlock), Pitfall 15 (LVGL thread safety)
**Effort:** 2-3 days
**Tools:** Code review for mutex ordering, BLE init/deinit lifecycle analysis

### Phase 5: Targeted Refactoring — Memory Pools
**Rationale:** If Phase 3 confirms frequent Packet/Bytes allocations cause fragmentation, implement pool allocators. High-impact but requires careful implementation.
**Delivers:** Pool-backed allocation for Packet::Object and Bytes, overflow handling
**Addresses:** Best practice: memory pools for same-size objects
**Avoids:** Pitfall 1 (eliminates variable-size allocations in hot path)
**Effort:** 1-2 weeks
**Tools:** Embedded Template Library (ETL) for production-ready pool implementations, custom pool allocator for shared_ptr

### Phase 6: Validation & Soak Testing
**Rationale:** Verify fixes under extended runtime. Cannot declare success without 48-72 hour stable operation.
**Delivers:** Validated stable firmware, updated configuration documentation
**Addresses:** Soak testing (best practice)
**Avoids:** Regression detection before production deployment
**Effort:** 3-5 days (mostly passive runtime)
**Tools:** Heap monitoring continues, automated stress testing

### Phase Ordering Rationale

- **Phase 1 first** because data-driven decisions beat guesswork. Heap monitoring confirms fragmentation hypothesis before committing to refactoring.
- **Phase 2 parallel/early** because boot time fixes are independent, low-risk config changes with immediate user benefit.
- **Phase 3 & 4 parallel** because memory and concurrency audits are independent analyses. Both inform refactoring priorities.
- **Phase 5 after audits** because pool refactoring is invasive. Only proceed if Phase 3 confirms it addresses root cause.
- **Phase 6 last** because validation requires all fixes in place. Soak testing catches integration issues.

### Research Flags

**Phases with standard patterns (skip research-phase):**
- **Phase 1:** Instrumentation uses well-documented ESP-IDF APIs, examples in STACK.md sufficient
- **Phase 2:** Boot optimization is configuration-only, all settings documented in FEATURES.md
- **Phase 4:** Concurrency patterns are FreeRTOS fundamentals, PITFALLS.md covers common mistakes

**Phases likely needing deeper research:**
- **Phase 5:** Pool allocator integration with shared_ptr may need architecture prototyping. Consider research spike if Embedded Template Library integration unclear.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | Official ESP-IDF documentation, PlatformIO native tooling |
| Features | HIGH | ESP-IDF and FreeRTOS official docs, community consensus on practices |
| Architecture | HIGH | ESP-IDF memory management docs, FreeRTOS kernel sources, ETL library examples |
| Pitfalls | HIGH | ESP-IDF official docs, verified ESP32 community issues, Espressif GitHub issues |

**Overall confidence:** HIGH

All research grounded in official Espressif documentation and verified community sources. Architecture patterns confirmed against ESP-IDF RAM optimization guides. Pitfalls cross-referenced with known ESP32 forum issues and GitHub bug reports (NimBLE leak, shared_ptr threading issues).

### Gaps to Address

**During Phase 1 (Instrumentation):**
- Confirm PSRAM allocation strategy: Are large buffers actually using PSRAM or falling back to internal RAM? Add MALLOC_CAP_INTERNAL vs MALLOC_CAP_SPIRAM breakdown to monitoring.

**During Phase 3 (Memory Audit):**
- Quantify shared_ptr overhead: How many Packet objects exist concurrently during typical operation? This determines pool size requirements for Phase 5.
- ArduinoJson usage patterns: Are DynamicJsonDocument instances pooled or allocated per operation? If latter, convert to StaticJsonDocument.

**During Phase 4 (Concurrency Audit):**
- NimBLE init/deinit lifecycle: Confirm stack is initialized once at boot. Check for any power-saving code that might trigger deinit cycles.
- LVGL mutex coverage: Audit all lv_* calls for missing mutex acquisition, especially in network event handlers.

**Before Phase 5 (Pool Refactoring):**
- Prototype pool allocator with shared_ptr: Validate std::allocate_shared pattern works with custom pool allocator before committing to full refactoring.

## Sources

### Primary (HIGH confidence)
- [ESP-IDF Heap Memory Debugging v5.5.2](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/heap_debug.html) — Fragmentation detection APIs, corruption detection modes
- [ESP-IDF Minimizing RAM Usage](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/ram-usage.html) — PSRAM strategy, stack sizing guidelines
- [ESP-IDF Speed Optimization](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/speed.html) — Boot time configuration, flash mode settings
- [ESP-IDF Watchdogs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/wdts.html) — Task watchdog integration, timeout handling
- [ESP-IDF Core Dump](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/core_dump.html) — Post-mortem analysis configuration
- [ESP-IDF External RAM Support](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/external-ram.html) — PSRAM allocation strategies, DMA limitations
- [FreeRTOS Kernel GitHub](https://github.com/freertos/freertos-kernel) — Mutex priority inheritance, task management

### Secondary (MEDIUM confidence)
- [ESP32 Forum: Heap Fragmentation](https://esp32.com/viewtopic.php?t=5646) — Community-verified fragmentation patterns
- [ESP32 Forum: NimBLE RAM Usage](https://esp32.com/viewtopic.php?t=33783) — NimBLE memory optimization techniques
- [espressif/esp-idf Issue #8136](https://github.com/espressif/esp-idf/issues/8136) — Confirmed NimBLE init/deinit memory leak
- [espressif/esp-idf Issue #3845](https://github.com/espressif/esp-idf/issues/3845) — Confirmed shared_ptr threading issue on ESP32
- [Embedded Template Library (ETL)](https://www.etlcpp.com/) — Production-ready pool implementations for embedded C++
- [PlatformIO Static Code Analysis](https://docs.platformio.org/en/latest/advanced/static-code-analysis/index.html) — Cppcheck and clang-tidy integration

### Tertiary (validation recommended)
- [ArduinoJson 7.4 Release Notes](https://arduinojson.org/news/2025/04/09/arduinojson-7-4/) — String optimization features, validate version compatibility
- [LVGL Forum: Memory Issues](https://forum.lvgl.io/t/memory-and-esp32/4050) — LVGL memory threshold recommendations (5KB free minimum)

---
*Research completed: 2026-01-23*
*Ready for roadmap: yes*
