#include "base/profile/cpu_timer.hpp"

#include "base/core/types.hpp"

#include <gtest/gtest.h>

using base::profile::read_cpu_timer;
using namespace base::types;

TEST(CpuTimer, Advances) {
    const u64 start = read_cpu_timer();
    u64 now = start;
    // Bounded, so a broken timer fails the test instead of hanging it.
    for (int i = 0; i < 10'000'000 && now == start; ++i) {
        now = read_cpu_timer();
    }
    EXPECT_GT(now, start);
}

TEST(CpuTimer, NeverGoesBackwards) {
    u64 previous = read_cpu_timer();
    for (int i = 0; i < 100'000; ++i) {
        const u64 now = read_cpu_timer();
        ASSERT_GE(now, previous);
        previous = now;
    }
}
