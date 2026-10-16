#pragma once

#include "base/core/config.hpp"

#include <cstdint>

#if BASE_COMPILER_MSVC
#include <intrin.h>
#elif BASE_ARCH_X64
#include <x86intrin.h>
#endif

namespace base::profile {

/**
 * \brief Reads the CPU's timestamp counter: `rdtsc` on x64, the generic timer
 * (`cntvct_el0`) on ARM64.
 *
 * Both count at a constant rate, independent of the current clock speed, and a read
 * costs a few nanoseconds, which is why profiling zones use it instead of an OS
 * clock. The rate is not known up front; see report(), which derives it from
 * std::chrono::steady_clock. The ARM64 counter is coarse (24 MHz on Apple silicon,
 * about 42 ns per tick), so very short zones read as 0 or 1 tick there.
 *
 * The read is not serializing: the CPU may reorder it with neighboring instructions
 * by a few cycles, which is noise at the scale of a profiling zone.
 */
[[nodiscard]] inline std::uint64_t read_cpu_timer() noexcept {
#if BASE_ARCH_X64
    return __rdtsc();
#elif BASE_COMPILER_MSVC
    return static_cast<std::uint64_t>(_ReadStatusReg(ARM64_CNTVCT));
#else
    std::uint64_t ticks = 0;
    asm volatile("mrs %0, cntvct_el0" : "=r"(ticks));
    return ticks;
#endif
}

} // namespace base::profile
