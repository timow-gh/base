#include "base/profile/heap_tracking.hpp"

#include "base/core/types.hpp"
#include "base/profile/profile.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

using base::profile::heap_stats;
using base::profile::HeapStats;
using base::profile::record_heap_allocation;
using base::profile::record_heap_deallocation;
using namespace base::types;

// This test program keeps the standard operator new, so only the calls in the tests are
// recorded and the counts are exact.
TEST(HeapTracking, RecordedAllocationsAddUp) {
    const HeapStats before = heap_stats();
    record_heap_allocation(100);
    record_heap_allocation(28);
    const HeapStats allocated = heap_stats();
    EXPECT_EQ(allocated.allocationCount - before.allocationCount, 2U);
    EXPECT_EQ(allocated.allocatedBytes - before.allocatedBytes, 128U);
    EXPECT_EQ(allocated.liveBytes - before.liveBytes, 128U);

    record_heap_deallocation(100);
    record_heap_deallocation(28);
    const HeapStats freed = heap_stats();
    EXPECT_EQ(freed.deallocationCount - before.deallocationCount, 2U);
    EXPECT_EQ(freed.freedBytes - before.freedBytes, 128U);
    EXPECT_EQ(freed.liveBytes, before.liveBytes);
}

TEST(HeapTracking, PeakKeepsTheHighestLiveBytesUntilReset) {
    base::profile::reset_heap_peak();
    const HeapStats before = heap_stats();
    EXPECT_EQ(before.peakLiveBytes, before.liveBytes);

    record_heap_allocation(1000);
    record_heap_deallocation(1000);
    record_heap_allocation(10);
    EXPECT_EQ(heap_stats().peakLiveBytes, before.liveBytes + 1000U);

    base::profile::reset_heap_peak();
    EXPECT_EQ(heap_stats().peakLiveBytes, before.liveBytes + 10U);
    record_heap_deallocation(10);
}

TEST(HeapTracking, FreeingMoreThanWasAllocatedDoesNotWrapAround) {
    const HeapStats before = heap_stats();
    // Like blocks freed through a recording operator delete that were never recorded.
    const u64 excess = before.liveBytes + 1000U;
    record_heap_deallocation(excess);
    EXPECT_EQ(heap_stats().liveBytes, 0U);
    record_heap_allocation(10);
    EXPECT_EQ(heap_stats().liveBytes, 0U);
    EXPECT_EQ(heap_stats().peakLiveBytes, before.peakLiveBytes);

    // Balances the books again for the other tests.
    record_heap_allocation(excess - 10U);
    EXPECT_EQ(heap_stats().liveBytes, before.liveBytes);
}

TEST(HeapTracking, ConcurrentRecordsAddUp) {
    constexpr u64 recordsPerThread = 10000;
    constexpr u64 threadCount = 4;
    constexpr usize blockSize = 8;
    const HeapStats before = heap_stats();
    {
        std::vector<std::jthread> threads;
        for (u64 thread = 0; thread < threadCount; ++thread) {
            threads.emplace_back([] {
                for (u64 i = 0; i < recordsPerThread; ++i) {
                    record_heap_allocation(blockSize);
                    record_heap_deallocation(blockSize);
                }
            });
        }
    }
    const HeapStats after = heap_stats();
    EXPECT_EQ(after.allocationCount - before.allocationCount, recordsPerThread * threadCount);
    EXPECT_EQ(after.deallocationCount - before.deallocationCount, recordsPerThread * threadCount);
    EXPECT_EQ(after.allocatedBytes - before.allocatedBytes, recordsPerThread * threadCount * blockSize);
    EXPECT_EQ(after.liveBytes, before.liveBytes);
}

TEST(HeapTracking, AllocationsAreChargedToTheOpenAllocationZone) {
    // Static, like the anchors of the zone macros: the session keeps a pointer to it.
    static base::profile::Anchor anchor{"heap_tracking.zone", base::profile::ZoneKind::allocations};
    {
        const base::profile::AllocationBlock block(anchor);
        record_heap_allocation(64);
    }
    record_heap_deallocation(64);
    EXPECT_EQ(anchor.allocationCount, 1U);
    EXPECT_EQ(anchor.allocatedBytes, 64U);
}

TEST(HeapTracking, TimeZonesCountNoAllocations) {
    static base::profile::Anchor anchor{"heap_tracking.time-zone"};
    {
        const base::profile::Block block(anchor);
        record_heap_allocation(64);
    }
    record_heap_deallocation(64);
    EXPECT_EQ(anchor.hitCount, 1U);
    EXPECT_EQ(anchor.allocationCount, 0U);
    EXPECT_EQ(anchor.allocatedBytes, 0U);
}
