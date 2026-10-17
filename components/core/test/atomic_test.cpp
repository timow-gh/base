#include "base/core/atomic.hpp"

#include "base/core/types.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace base::types;

TEST(AtomicFetchMax, RaisesALowerValueAndReturnsThePreviousOne) {
    std::atomic<u64> peak{10};
    EXPECT_EQ(base::atomic_fetch_max(peak, 25), 10U);
    EXPECT_EQ(peak.load(), 25U);
}

TEST(AtomicFetchMax, KeepsAHigherValue) {
    std::atomic<u64> peak{25};
    EXPECT_EQ(base::atomic_fetch_max(peak, 10), 25U);
    EXPECT_EQ(peak.load(), 25U);
}

TEST(AtomicFetchMax, ConcurrentCallsKeepTheLargestValue) {
    constexpr u64 valuesPerThread = 10000;
    constexpr u64 threadCount = 4;
    std::atomic<u64> peak{0};
    {
        std::vector<std::jthread> threads;
        for (u64 thread = 0; thread < threadCount; ++thread) {
            threads.emplace_back([&peak, thread] {
                for (u64 i = 0; i < valuesPerThread; ++i) {
                    base::atomic_fetch_max(peak, (i * threadCount) + thread);
                }
            });
        }
    }
    EXPECT_EQ(peak.load(), (valuesPerThread * threadCount) - 1U);
}
