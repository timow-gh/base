#include "base/core/alignment.hpp"
#include "base/core/config.hpp"
#include "base/core/types.hpp"
#include "base/profile/heap_tracking.hpp"
#include "base/profile/profile.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <new>
#include <vector>

using base::profile::heap_stats;
using base::profile::HeapStats;
using namespace base::types;

namespace {

// Compilers may leave out a new-expression whose result is never used, which would
// leave nothing to record. Publishing the pointer keeps it.
void* volatile publishedPointer = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

template <typename T>
T* publish(T* pointer) {
    publishedPointer = pointer;
    return pointer;
}

struct alignas(64) CacheLine {
    std::array<std::byte, 64> bytes;
};

[[nodiscard]] u64 allocations_since(const HeapStats& before) {
    return heap_stats().allocationCount - before.allocationCount;
}

// Huge requests make AddressSanitizer abort instead of returning nullptr, unless it
// runs with allocator_may_return_null=1.
[[nodiscard]] bool huge_allocations_fail_gracefully() {
    return BASE_HAS_ASAN == 0;
}

int newHandlerCalls = 0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void give_up_after_one_call() {
    ++newHandlerCalls;
    std::set_new_handler(nullptr);
}

} // namespace

TEST(GlobalNew, NewAndDeleteAreRecorded) {
    const HeapStats before = heap_stats();
    int* value = publish(new int(42));
    const HeapStats allocated = heap_stats();
    EXPECT_EQ(allocated.allocationCount - before.allocationCount, 1U);
    EXPECT_GE(allocated.allocatedBytes - before.allocatedBytes, sizeof(int));
    EXPECT_GE(allocated.peakLiveBytes, allocated.liveBytes);

    delete value;
    const HeapStats freed = heap_stats();
    EXPECT_EQ(freed.deallocationCount - allocated.deallocationCount, 1U);
    EXPECT_EQ(freed.freedBytes - allocated.freedBytes, allocated.allocatedBytes - before.allocatedBytes);
    EXPECT_EQ(freed.liveBytes, before.liveBytes);
}

TEST(GlobalNew, ArrayNewAndDeleteAreRecorded) {
    const HeapStats before = heap_stats();
    int* values = publish(new int[100]);
    EXPECT_EQ(allocations_since(before), 1U);
    EXPECT_GE(heap_stats().allocatedBytes - before.allocatedBytes, 100U * sizeof(int));
    delete[] values;
    EXPECT_EQ(heap_stats().liveBytes, before.liveBytes);
}

TEST(GlobalNew, OverAlignedNewIsAlignedAndRecorded) {
    const HeapStats before = heap_stats();
    CacheLine* single = publish(new CacheLine{});
    CacheLine* array = publish(new CacheLine[3]);
    EXPECT_TRUE(base::is_aligned(reinterpret_cast<uptr>(single), uptr{64}));
    EXPECT_TRUE(base::is_aligned(reinterpret_cast<uptr>(array), uptr{64}));
    EXPECT_EQ(allocations_since(before), 2U);
    delete single;
    delete[] array;
    EXPECT_EQ(heap_stats().liveBytes, before.liveBytes);
}

TEST(GlobalNew, NothrowNewIsRecorded) {
    const HeapStats before = heap_stats();
    int* value = publish(new (std::nothrow) int(7));
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(allocations_since(before), 1U);
    delete value;
    EXPECT_EQ(heap_stats().liveBytes, before.liveBytes);
}

TEST(GlobalNew, ZeroBytesGiveAUniquePointer) {
    void* first = ::operator new(0);
    void* second = ::operator new(0);
    EXPECT_NE(first, second);
    ::operator delete(first);
    ::operator delete(second);
}

TEST(GlobalNew, GrowingVectorIsRecorded) {
    const HeapStats before = heap_stats();
    {
        std::vector<int> values;
        for (int i = 0; i < 1000; ++i) {
            values.push_back(i);
        }
        publish(values.data());
        EXPECT_GE(allocations_since(before), 2U);
    }
    EXPECT_EQ(heap_stats().liveBytes, before.liveBytes);
}

TEST(GlobalNew, ImpossibleSizeThrowsOrReturnsNullptr) {
    if (!huge_allocations_fail_gracefully()) {
        GTEST_SKIP() << "AddressSanitizer aborts on huge allocations";
    }
    constexpr usize impossible = std::numeric_limits<usize>::max() / 2U;
    // Caught by hand: the assertion macros allocate, which would be counted too.
    const HeapStats before = heap_stats();
    bool plainThrew = false;
    try {
        publish(::operator new(impossible));
    } catch (const std::bad_alloc&) {
        plainThrew = true;
    }
    bool alignedThrew = false;
    try {
        publish(::operator new(impossible, std::align_val_t{64}));
    } catch (const std::bad_alloc&) {
        alignedThrew = true;
    }
    void* plainNothrow = ::operator new(impossible, std::nothrow);
    void* alignedNothrow = ::operator new(impossible, std::align_val_t{64}, std::nothrow);
    const u64 recorded = allocations_since(before);

    EXPECT_TRUE(plainThrew);
    EXPECT_TRUE(alignedThrew);
    EXPECT_EQ(plainNothrow, nullptr);
    EXPECT_EQ(alignedNothrow, nullptr);
    EXPECT_EQ(recorded, 0U);
}

TEST(GlobalNew, NewHandlerRunsBeforeTheThrow) {
    if (!huge_allocations_fail_gracefully()) {
        GTEST_SKIP() << "AddressSanitizer aborts on huge allocations";
    }
    newHandlerCalls = 0;
    std::set_new_handler(give_up_after_one_call);
    EXPECT_THROW(publish(::operator new(std::numeric_limits<usize>::max() / 2U)), std::bad_alloc);
    EXPECT_EQ(newHandlerCalls, 1);
    EXPECT_EQ(std::get_new_handler(), nullptr);
}

// Explicit allocation zones rather than the zone macros, which create time zones unless
// base is configured with base_PROFILING=ALLOCATIONS.
TEST(GlobalNew, AllocationsAreChargedToTheInnermostAllocationZone) {
    using base::profile::Anchor;
    using base::profile::AllocationBlock;
    using base::profile::ZoneKind;

    // Static, like the anchors of the zone macros: the session keeps pointers to them.
    static Anchor outer{"global_new.outer", ZoneKind::allocations};
    static Anchor inner{"global_new.inner", ZoneKind::allocations};
    {
        const AllocationBlock outerBlock(outer);
        delete publish(new int(1));
        {
            const AllocationBlock innerBlock(inner);
            delete publish(new int(2));
            delete publish(new int(3));
        }
    }
    EXPECT_EQ(outer.allocationCount, 1U);
    EXPECT_EQ(inner.allocationCount, 2U);
    EXPECT_GE(inner.allocatedBytes, 2U * sizeof(int));
}
