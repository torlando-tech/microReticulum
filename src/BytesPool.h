#pragma once

/**
 * BytesPool.h - Pool for Bytes Data objects to prevent heap fragmentation
 *
 * Problem: Bytes uses shared_ptr<Data> where Data = vector<uint8_t>.
 * Each make_shared allocates a control block (24 bytes) + vector metadata.
 * In long-running firmware, this causes heap fragmentation.
 *
 * Solution: Pool the Data objects (vectors) themselves. When a Bytes object
 * needs storage for common sizes (<=1024 bytes), it gets a pre-allocated
 * Data from the pool with capacity already reserved. When the shared_ptr
 * refcount hits 0, a custom deleter returns the Data to the pool instead
 * of destroying it.
 *
 * The pool has three tiers sized for Reticulum packet processing:
 *   - 256 bytes: hashes, keys, small announces
 *   - 512 bytes: standard packets (MTU=500 + margin)
 *   - 1024 bytes: resource advertisements, large packets
 *
 * Thread-safe via FreeRTOS spinlock (ESP32) or std::mutex (native).
 *
 * Usage (in Bytes.cpp):
 *   auto [data, tier] = BytesPool::instance().acquire(capacity);
 *   if (data) {
 *       // Got pooled Data - use with custom deleter
 *       _data = SharedData(data, BytesPoolDeleter{tier});
 *   } else {
 *       // Pool exhausted - fall back to make_shared
 *       _data = std::make_shared<Data>();
 *   }
 */

#include "PSRAMAllocator.h"
#include "Log.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// FreeRTOS spinlock support - only on ESP32
#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#define BYTESPOOL_USE_SPINLOCK 1
#else
// Native build - use std::mutex instead
#include <mutex>
#define BYTESPOOL_USE_SPINLOCK 0
#endif

namespace RNS {

// Pool configuration - sized for Reticulum packet processing
// MTU=500, most packets fit in 512 bytes, large resources may need 1024
namespace BytesPoolConfig {
    static constexpr size_t TIER_SMALL = 256;     // Small packets, hashes, keys
    static constexpr size_t TIER_MEDIUM = 512;    // Standard packets
    static constexpr size_t TIER_LARGE = 1024;    // Large packets, resource ads
    static constexpr size_t SLOTS_PER_TIER = 16;  // 16 buffers per tier

    // Tier identifiers for deleter
    enum Tier : uint8_t {
        TIER_NONE = 0,    // Not from pool (fallback allocation)
        TIER_256 = 1,
        TIER_512 = 2,
        TIER_1024 = 3
    };
}

// Forward declaration - Data is vector with PSRAMAllocator
using PooledData = std::vector<uint8_t, PSRAMAllocator<uint8_t>>;

/**
 * Pool for Bytes Data objects (vectors).
 *
 * Each tier maintains a stack of pre-allocated vectors with capacity reserved.
 * Vectors are cleared (size=0) but capacity preserved when returned to pool.
 *
 * This eliminates:
 *   - Repeated vector construction/destruction
 *   - Repeated capacity reservation allocations
 *   - shared_ptr control block allocations (via make_shared replacement)
 *
 * Memory footprint per tier:
 *   - 16 slots x ~24 bytes (vector metadata) = ~384 bytes per tier
 *   - Plus backing storage: 16 x tier_size bytes
 *   - Total for 3 tiers: ~1.2KB metadata + ~28KB backing = ~30KB
 */
class BytesPool {
public:
    // Singleton access - pool must be global for custom deleter
    static BytesPool& instance() {
        static BytesPool pool;
        return pool;
    }

    /**
     * Acquire a Data object from pool.
     * Returns {pointer, tier} or {nullptr, TIER_NONE} if pool exhausted.
     *
     * The returned Data is empty (size=0) but has capacity >= requested.
     * Caller must use the tier value to construct BytesPoolDeleter.
     */
    std::pair<PooledData*, BytesPoolConfig::Tier> acquire(size_t requested_capacity) {
        _total_requests++;

#if BYTESPOOL_USE_SPINLOCK
        portENTER_CRITICAL(&_mux);
#else
        std::lock_guard<std::mutex> lock(_mutex);
#endif

        PooledData* result = nullptr;
        BytesPoolConfig::Tier tier = BytesPoolConfig::TIER_NONE;

        // Try smallest tier that fits
        if (requested_capacity <= BytesPoolConfig::TIER_SMALL) {
            if (_small_count > 0) {
                result = _small_stack[--_small_count];
                tier = BytesPoolConfig::TIER_256;
                _pool_hits++;
            }
        }
        else if (requested_capacity <= BytesPoolConfig::TIER_MEDIUM) {
            if (_medium_count > 0) {
                result = _medium_stack[--_medium_count];
                tier = BytesPoolConfig::TIER_512;
                _pool_hits++;
            }
        }
        else if (requested_capacity <= BytesPoolConfig::TIER_LARGE) {
            if (_large_count > 0) {
                result = _large_stack[--_large_count];
                tier = BytesPoolConfig::TIER_1024;
                _pool_hits++;
            }
        }
        // Oversized requests fall through with nullptr

        if (!result) {
            _pool_misses++;
        }

#if BYTESPOOL_USE_SPINLOCK
        portEXIT_CRITICAL(&_mux);
#endif

        return {result, tier};
    }

    /**
     * Release a Data object back to pool.
     * Called by BytesPoolDeleter when shared_ptr refcount hits 0.
     *
     * The Data is cleared (preserving capacity) and pushed to tier stack.
     */
    void release(PooledData* data, BytesPoolConfig::Tier tier) {
        if (!data || tier == BytesPoolConfig::TIER_NONE) {
            // Not from pool - should not happen, but defensive
            return;
        }

        // Clear but preserve capacity
        data->clear();

#if BYTESPOOL_USE_SPINLOCK
        portENTER_CRITICAL(&_mux);
#else
        std::lock_guard<std::mutex> lock(_mutex);
#endif

        switch (tier) {
            case BytesPoolConfig::TIER_256:
                if (_small_count < BytesPoolConfig::SLOTS_PER_TIER) {
                    _small_stack[_small_count++] = data;
                }
                // else pool full - data leaks (shouldn't happen in normal operation)
                break;
            case BytesPoolConfig::TIER_512:
                if (_medium_count < BytesPoolConfig::SLOTS_PER_TIER) {
                    _medium_stack[_medium_count++] = data;
                }
                break;
            case BytesPoolConfig::TIER_1024:
                if (_large_count < BytesPoolConfig::SLOTS_PER_TIER) {
                    _large_stack[_large_count++] = data;
                }
                break;
            default:
                break;
        }

#if BYTESPOOL_USE_SPINLOCK
        portEXIT_CRITICAL(&_mux);
#endif
    }

    // Instrumentation
    size_t total_requests() const { return _total_requests; }
    size_t pool_hits() const { return _pool_hits; }
    size_t pool_misses() const { return _pool_misses; }
    float hit_rate() const {
        return _total_requests > 0 ? (float)_pool_hits / _total_requests : 0.0f;
    }

    // Current pool state
    size_t small_available() const { return _small_count; }
    size_t medium_available() const { return _medium_count; }
    size_t large_available() const { return _large_count; }
    size_t small_in_use() const { return BytesPoolConfig::SLOTS_PER_TIER - _small_count; }
    size_t medium_in_use() const { return BytesPoolConfig::SLOTS_PER_TIER - _medium_count; }
    size_t large_in_use() const { return BytesPoolConfig::SLOTS_PER_TIER - _large_count; }

    // Log statistics for tuning
    void logStats() const {
        INFOF("BytesPool: requests=%zu hits=%zu misses=%zu hit_rate=%d%% "
              "small=%zu/%zu med=%zu/%zu large=%zu/%zu",
              _total_requests, _pool_hits, _pool_misses,
              (int)(hit_rate() * 100),
              small_in_use(), BytesPoolConfig::SLOTS_PER_TIER,
              medium_in_use(), BytesPoolConfig::SLOTS_PER_TIER,
              large_in_use(), BytesPoolConfig::SLOTS_PER_TIER);
    }

private:
    BytesPool() {
#if BYTESPOOL_USE_SPINLOCK
        portMUX_INITIALIZE(&_mux);
#endif
        // Pre-allocate all pool entries
        // This is done at construction (startup) to front-load allocations
        initializeTier(_small_storage, _small_stack, _small_count,
                       BytesPoolConfig::TIER_SMALL, BytesPoolConfig::SLOTS_PER_TIER);
        initializeTier(_medium_storage, _medium_stack, _medium_count,
                       BytesPoolConfig::TIER_MEDIUM, BytesPoolConfig::SLOTS_PER_TIER);
        initializeTier(_large_storage, _large_stack, _large_count,
                       BytesPoolConfig::TIER_LARGE, BytesPoolConfig::SLOTS_PER_TIER);
    }

    // Non-copyable
    BytesPool(const BytesPool&) = delete;
    BytesPool& operator=(const BytesPool&) = delete;

    // Initialize a tier with pre-allocated vectors
    void initializeTier(PooledData* storage, PooledData** stack, size_t& count,
                        size_t capacity, size_t slots) {
        for (size_t i = 0; i < slots; i++) {
            // Placement new to construct in storage array
            new (&storage[i]) PooledData();
            storage[i].reserve(capacity);
            stack[i] = &storage[i];
        }
        count = slots;
    }

    // Storage for pooled vectors (fixed arrays avoid dynamic allocation)
    PooledData _small_storage[BytesPoolConfig::SLOTS_PER_TIER];
    PooledData _medium_storage[BytesPoolConfig::SLOTS_PER_TIER];
    PooledData _large_storage[BytesPoolConfig::SLOTS_PER_TIER];

    // Stacks of available vectors (indices into storage arrays)
    PooledData* _small_stack[BytesPoolConfig::SLOTS_PER_TIER];
    PooledData* _medium_stack[BytesPoolConfig::SLOTS_PER_TIER];
    PooledData* _large_stack[BytesPoolConfig::SLOTS_PER_TIER];

    // Stack counts (how many available in each tier)
    size_t _small_count = 0;
    size_t _medium_count = 0;
    size_t _large_count = 0;

    // Instrumentation counters
    size_t _total_requests = 0;
    size_t _pool_hits = 0;
    size_t _pool_misses = 0;

#if BYTESPOOL_USE_SPINLOCK
    portMUX_TYPE _mux;
#else
    std::mutex _mutex;
#endif
};

/**
 * Custom deleter for shared_ptr that returns Data to pool.
 *
 * When shared_ptr refcount hits 0, this deleter is called instead of delete.
 * The Data is returned to the appropriate tier in BytesPool.
 */
struct BytesPoolDeleter {
    BytesPoolConfig::Tier tier;

    explicit BytesPoolDeleter(BytesPoolConfig::Tier t = BytesPoolConfig::TIER_NONE)
        : tier(t) {}

    void operator()(PooledData* data) const {
        if (data && tier != BytesPoolConfig::TIER_NONE) {
            // Return to pool instead of destroying
            BytesPool::instance().release(data, tier);
        }
        // Note: We never delete the data - it lives in BytesPool's storage arrays
        // If tier is TIER_NONE, this is a fallback allocation and won't be called
    }
};

} // namespace RNS
