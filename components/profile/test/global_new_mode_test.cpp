#include "base/core/config.hpp"
#include "base/core/types.hpp"
#include "base/profile/heap_tracking.hpp"

#include <gtest/gtest.h>

using base::profile::heap_stats;
using base::profile::HeapStats;
using namespace base::types;

namespace {

// Keeps the compiler from leaving out the new-expression, see global_new_test.cpp.
int* volatile publishedPointer = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

} // namespace

// Linked against base::profile_global_new as a consumer links it: in any other build
// than an allocation build it must leave operator new alone, so that a time
// measurement is not distorted by tracking.
TEST(GlobalNewMode, ReplacesOperatorNewInAllocationBuildsOnly) {
    constexpr u64 expectedAllocations = BASE_PROFILE_ALLOCATIONS != 0 ? 1U : 0U;
    // Read before any assertion: the assertion macros may allocate.
    const HeapStats before = heap_stats();
    publishedPointer = new int(1);
    delete publishedPointer;
    const HeapStats after = heap_stats();
    EXPECT_EQ(after.allocationCount - before.allocationCount, expectedAllocations);
    EXPECT_EQ(after.deallocationCount - before.deallocationCount, expectedAllocations);
}
