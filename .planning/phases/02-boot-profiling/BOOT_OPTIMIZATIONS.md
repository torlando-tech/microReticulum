# Boot Optimizations - T-Deck Firmware

**Created:** 2026-01-24
**Phase:** 02-boot-profiling

## Overview

This document tracks all configuration changes made to optimize boot time.
Each optimization is independently toggleable to allow testing individual impact.

## Implemented Optimizations

### 1. Disable PSRAM Memory Test

**File:** `examples/lxmf_tdeck/sdkconfig.defaults`
**Setting:** `CONFIG_SPIRAM_MEMTEST=n`
**Expected savings:** ~2 seconds (250ms per MB for 8MB PSRAM)

The PSRAM memory test writes and reads patterns to verify memory integrity. While useful for detecting hardware issues, it adds significant time to boot. For stable production hardware, this test can be safely disabled.

**To re-enable:** Set `CONFIG_SPIRAM_MEMTEST=y` or remove the line from sdkconfig.defaults.

### 2. Reduce Bootloader Log Level

**File:** `examples/lxmf_tdeck/sdkconfig.defaults`
**Settings:**
- `CONFIG_BOOTLOADER_LOG_LEVEL_WARN=y`
- `CONFIG_BOOTLOADER_LOG_LEVEL=2`
- `CONFIG_BOOT_ROM_LOG_ALWAYS_OFF=y`

**Expected savings:** ~100-200ms

Reduces serial output during early boot stages. Bootloader messages are useful for debugging but add latency.

**To re-enable:** Comment out or remove these lines from sdkconfig.defaults.

### 3. Application Log Level (Optional)

**File:** `examples/lxmf_tdeck/platformio.ini`
**Current setting:** `CORE_DEBUG_LEVEL=3` (INFO)
**Production option:** Change to 2 (WARNING) or 1 (ERROR)

**Expected savings:** ~200-500ms depending on log volume

Not enabled by default to preserve debugging information during development.
See commented `BOOT_REDUCED_LOGGING` flag in platformio.ini.

**To enable:** Change `-DCORE_DEBUG_LEVEL=3` to `-DCORE_DEBUG_LEVEL=2`

## Verified Optimal Settings

These settings were verified as already optimal:

### Flash Mode Configuration

**File:** `examples/lxmf_tdeck/platformio.ini`
- `board_build.flash_mode = qio` (Quad I/O - fastest mode)
- `board_build.arduino.memory_type = qio_opi` (Octal PI for PSRAM)

No changes needed - already configured for maximum speed.

### PSRAM Speed

**File:** `examples/lxmf_tdeck/sdkconfig.defaults`
- `CONFIG_SPIRAM_SPEED_80M=y` (80MHz - maximum speed)
- `CONFIG_SPIRAM_MODE_OCT=y` (Octal mode - maximum bandwidth)

Already optimal.

## Blocking Operations Identified

These are code-level issues that require refactoring (not configuration changes):

| Operation | Location | Current Timeout | Impact |
|-----------|----------|-----------------|--------|
| WiFi connect | setup_wifi() | 30 seconds | Blocks if no WiFi |
| GPS time sync | setup() | 15 seconds | Blocks if no GPS fix |
| TCP stabilization | setup_lxmf() | 3 seconds fixed | Always blocks |

Recommendations:
1. Reduce WiFi timeout to 10 seconds
2. Move GPS sync to background or reduce timeout to 5 seconds
3. Reduce TCP delay to 1 second or use connection callback

These are tracked for potential future optimization but not implemented in this phase.

## Validation

After applying optimizations, run boot profiling to measure actual improvement:

1. Build with `BOOT_PROFILING_ENABLED` defined
2. Flash firmware to T-Deck
3. Capture serial output during boot
4. Compare total boot time to baseline
5. Document results in this file

### Build Command

```bash
cd examples/lxmf_tdeck
pio run -e tdeck
```

### Expected Serial Output

With boot profiling enabled, you should see output like:

```
[BOOT] Phase 'hardware' started
[BOOT] Phase 'hardware' completed in 150ms
[BOOT] Phase 'lvgl' started
...
[BOOT] Boot complete in 4500ms
```

## Baseline Measurements

_To be filled after boot profiling data collection_

| Phase | Before Optimization | After Optimization | Savings |
|-------|--------------------|--------------------|---------|
| Total boot | TBD | TBD | TBD |
| PSRAM test | ~2000ms | Disabled | ~2000ms |
| Bootloader logs | TBD | TBD | TBD |
| App init logs | TBD | TBD | TBD |

## Summary of Changes

| Optimization | File | Expected Savings | Status |
|-------------|------|------------------|--------|
| Disable PSRAM test | sdkconfig.defaults | ~2000ms | Enabled |
| Reduce bootloader log | sdkconfig.defaults | ~150ms | Enabled |
| Skip ROM boot log | sdkconfig.defaults | ~50ms | Enabled |
| Reduce app log level | platformio.ini | ~300ms | Optional (commented) |

**Total expected savings:** ~2200ms+ (with PSRAM test disabled)

---
*Last updated: 2026-01-24*
