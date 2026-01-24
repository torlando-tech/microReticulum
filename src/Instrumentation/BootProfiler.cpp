/*
 * BootProfiler - Boot sequence timing instrumentation for ESP32
 *
 * Implementation uses millis() for timing (millisecond precision).
 * All buffers are statically allocated to avoid dynamic memory during boot.
 *
 * Key design decisions:
 *   - First markStart() call establishes boot start time
 *   - Phase timing tracks both individual duration and cumulative total
 *   - Wait time is tracked separately to distinguish I/O from CPU work
 *   - bootComplete() logs final summary for analysis
 */

#include "BootProfiler.h"

#ifdef BOOT_PROFILING_ENABLED

#include "../Log.h"

#include <Arduino.h>
#include <cstring>

namespace RNS { namespace Instrumentation {

// Static member initialization
uint32_t BootProfiler::_boot_start_ms = 0;
uint32_t BootProfiler::_cumulative_ms = 0;
char BootProfiler::_current_phase[32] = {0};
uint32_t BootProfiler::_phase_start_ms = 0;
char BootProfiler::_current_wait[32] = {0};
uint32_t BootProfiler::_wait_start_ms = 0;
uint32_t BootProfiler::_total_wait_ms = 0;
char BootProfiler::_log_buffer[256] = {0};


void BootProfiler::markStart(const char* phase) {
    uint32_t now = millis();

    // First call establishes boot start time
    if (_boot_start_ms == 0) {
        _boot_start_ms = now;
        NOTICE("[BOOT] Profiling started");
    }

    // Store current phase info
    strncpy(_current_phase, phase, sizeof(_current_phase) - 1);
    _current_phase[sizeof(_current_phase) - 1] = '\0';
    _phase_start_ms = now;

    snprintf(_log_buffer, sizeof(_log_buffer),
             "[BOOT] START: %s (at %ums)",
             phase, now - _boot_start_ms);
    NOTICE(_log_buffer);
}


void BootProfiler::markEnd(const char* phase) {
    uint32_t now = millis();

    // Calculate phase duration
    uint32_t duration = 0;
    if (_phase_start_ms > 0) {
        duration = now - _phase_start_ms;
    }

    // Update cumulative time
    _cumulative_ms = now - _boot_start_ms;

    snprintf(_log_buffer, sizeof(_log_buffer),
             "[BOOT] END: %s (%ums, cumulative: %ums)",
             phase, duration, _cumulative_ms);
    NOTICE(_log_buffer);

    // Clear current phase
    _current_phase[0] = '\0';
    _phase_start_ms = 0;
}


void BootProfiler::markWaitStart(const char* phase) {
    // Store wait info
    strncpy(_current_wait, phase, sizeof(_current_wait) - 1);
    _current_wait[sizeof(_current_wait) - 1] = '\0';
    _wait_start_ms = millis();
}


void BootProfiler::markWaitEnd(const char* phase) {
    uint32_t now = millis();

    // Calculate wait duration
    uint32_t duration = 0;
    if (_wait_start_ms > 0) {
        duration = now - _wait_start_ms;
    }

    // Accumulate wait time
    _total_wait_ms += duration;

    snprintf(_log_buffer, sizeof(_log_buffer),
             "[BOOT] WAIT: %s (%ums, total wait: %ums)",
             phase, duration, _total_wait_ms);
    NOTICE(_log_buffer);

    // Clear current wait
    _current_wait[0] = '\0';
    _wait_start_ms = 0;
}


void BootProfiler::bootComplete() {
    uint32_t now = millis();

    // Calculate final timings
    uint32_t total_ms = 0;
    if (_boot_start_ms > 0) {
        total_ms = now - _boot_start_ms;
    }

    uint32_t init_ms = total_ms - _total_wait_ms;

    snprintf(_log_buffer, sizeof(_log_buffer),
             "[BOOT] COMPLETE: total=%ums, init=%ums, wait=%ums",
             total_ms, init_ms, _total_wait_ms);
    NOTICE(_log_buffer);

    // Additional detail if wait time is significant
    if (_total_wait_ms > 0) {
        uint8_t wait_pct = (uint8_t)((_total_wait_ms * 100) / total_ms);
        snprintf(_log_buffer, sizeof(_log_buffer),
                 "[BOOT] Wait time: %u%% of boot",
                 wait_pct);
        NOTICE(_log_buffer);
    }
}


uint32_t BootProfiler::getTotalMs() {
    if (_boot_start_ms == 0) {
        return 0;
    }
    return millis() - _boot_start_ms;
}


uint32_t BootProfiler::getInitMs() {
    uint32_t total = getTotalMs();
    if (total < _total_wait_ms) {
        return 0;
    }
    return total - _total_wait_ms;
}


uint32_t BootProfiler::getWaitMs() {
    return _total_wait_ms;
}

}} // namespace RNS::Instrumentation

#endif // BOOT_PROFILING_ENABLED
