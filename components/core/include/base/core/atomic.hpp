#pragma once

#include "base/core/types.hpp"

#include <atomic>

namespace base {

/**
 * \brief Raises `target` to `value` if it is lower and returns the previous value, like
 * C++26's std::atomic::fetch_max, which can replace it once the compilers have it.
 *
 * Lock-free; concurrent calls keep the largest value. Relaxed, because its users keep
 * peaks, statistics that order no other memory. Call it qualified, as
 * base::atomic_fetch_max(): argument-dependent lookup would also find C++26's
 * std::atomic_fetch_max.
 */
inline u64 atomic_fetch_max(std::atomic<u64>& target, u64 value) noexcept {
    u64 current = target.load(std::memory_order_relaxed);
    while (current < value && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
    return current;
}

} // namespace base
