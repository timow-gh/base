#include "base/profile/cpu_timer.hpp"

#include <gtest/gtest.h>

#include <cstdint>

using base::profile::read_cpu_timer;

TEST(CpuTimer, Advances) {
    const std::uint64_t start = read_cpu_timer();
    std::uint64_t now = start;
    // Bounded, so a broken timer fails the test instead of hanging it.
    for (int i = 0; i < 10'000'000 && now == start; ++i) {
        now = read_cpu_timer();
    }
    EXPECT_GT(now, start);
}

TEST(CpuTimer, NeverGoesBackwards) {
    std::uint64_t previous = read_cpu_timer();
    for (int i = 0; i < 100'000; ++i) {
        const std::uint64_t now = read_cpu_timer();
        ASSERT_GE(now, previous);
        previous = now;
    }
}
