// Allocation zones regardless of base_PROFILING, see profile_test.cpp.
#undef BASE_ENABLE_PROFILING
#define BASE_ENABLE_PROFILING 1
#undef BASE_PROFILE_ALLOCATIONS
#define BASE_PROFILE_ALLOCATIONS 1

#include "base/core/types.hpp"
#include "base/profile/heap_tracking.hpp"
#include "base/profile/profile.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

using base::profile::record_heap_allocation;
using base::profile::record_heap_deallocation;
using base::profile::ZoneKind;
using base::profile::ZoneStats;
using namespace base::types;

namespace {

// The calls stand in for a recording operator new: this test program keeps the
// standard one.
void allocate_and_free(usize bytes) {
    record_heap_allocation(bytes);
    record_heap_deallocation(bytes);
}

[[nodiscard]] ZoneStats stats_of(std::string_view name) {
    const std::optional<ZoneStats> stats = base::profile::zone_stats(name);
    EXPECT_TRUE(stats.has_value()) << name;
    return stats.value_or(ZoneStats{});
}

[[nodiscard]] std::string report_text() {
    std::FILE* file = std::tmpfile();
    EXPECT_NE(file, nullptr);
    if (file == nullptr) {
        return {};
    }
    base::profile::print_report(file);
    std::rewind(file);
    std::string text;
    std::array<char, 256> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), file) != nullptr) {
        text += buffer.data();
    }
    std::fclose(file);
    return text;
}

class ProfileAllocationsTest : public ::testing::Test {
protected:
    void SetUp() override { base::profile::reset(); }
};

} // namespace

TEST_F(ProfileAllocationsTest, MacrosCreateAllocationZones) {
    {
        BASE_PROFILE_SCOPE("allocs.kind");
    }
    const ZoneStats stats = stats_of("allocs.kind");
    EXPECT_EQ(stats.kind, ZoneKind::allocations);
    EXPECT_EQ(stats.hitCount, 1U);
    EXPECT_EQ(stats.exclusiveTicks, 0U);
}

TEST_F(ProfileAllocationsTest, AllocationsAreChargedToTheInnermostZone) {
    {
        BASE_PROFILE_SCOPE("allocs.outer");
        allocate_and_free(16);
        {
            BASE_PROFILE_SCOPE("allocs.inner");
            allocate_and_free(8);
            allocate_and_free(24);
        }
    }
    const ZoneStats outer = stats_of("allocs.outer");
    const ZoneStats inner = stats_of("allocs.inner");
    EXPECT_EQ(outer.allocationCount, 1U);
    EXPECT_EQ(outer.allocatedBytes, 16U);
    EXPECT_EQ(inner.allocationCount, 2U);
    EXPECT_EQ(inner.allocatedBytes, 32U);
}

TEST_F(ProfileAllocationsTest, AllocationsOutsideZonesAreNotCharged) {
    allocate_and_free(16);
    {
        BASE_PROFILE_SCOPE("allocs.empty");
    }
    EXPECT_EQ(stats_of("allocs.empty").allocationCount, 0U);
}

TEST_F(ProfileAllocationsTest, RecursiveZoneCountsEachAllocationOnce) {
    struct Recursion {
        static void run(int depth) {
            BASE_PROFILE_SCOPE("allocs.recursion");
            allocate_and_free(10);
            if (depth > 0) {
                run(depth - 1);
            }
        }
    };
    Recursion::run(5);
    const ZoneStats stats = stats_of("allocs.recursion");
    EXPECT_EQ(stats.hitCount, 6U);
    EXPECT_EQ(stats.allocationCount, 6U);
    EXPECT_EQ(stats.allocatedBytes, 60U);
}

TEST_F(ProfileAllocationsTest, ResetResetsTheAllocations) {
    {
        BASE_PROFILE_SCOPE("allocs.reset");
        allocate_and_free(16);
    }
    base::profile::reset();
    EXPECT_EQ(stats_of("allocs.reset").allocationCount, 0U);
    EXPECT_EQ(stats_of("allocs.reset").allocatedBytes, 0U);
}

// The byte count of an allocation zone has nothing to be related to, so, as in disabled
// builds, it is not evaluated. Clang rightly warns about side effects in an unevaluated
// operand; here they are the point of the test.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunevaluated-expression"
#endif
TEST_F(ProfileAllocationsTest, BandwidthZonesDoNotEvaluateTheByteCount) {
    int evaluations = 0;
    {
        BASE_PROFILE_BANDWIDTH("allocs.bandwidth", ++evaluations);
    }
    EXPECT_EQ(evaluations, 0);
    EXPECT_EQ(stats_of("allocs.bandwidth").processedBytes, 0U);
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

TEST_F(ProfileAllocationsTest, ReportShowsAllocationsAndNoTimes) {
    {
        BASE_PROFILE_SCOPE("allocs.report");
        allocate_and_free(12345);
    }
    const std::string report = report_text();
    EXPECT_NE(report.find("Allocations in zones: 1 (12345 bytes)"), std::string::npos) << report;
    EXPECT_NE(report.find("allocs.report"), std::string::npos) << report;
    EXPECT_NE(report.find("alloc bytes"), std::string::npos) << report;
    EXPECT_EQ(report.find("Total:"), std::string::npos) << report;
    EXPECT_EQ(report.find("exclusive"), std::string::npos) << report;
}

TEST_F(ProfileAllocationsTest, ReportListsTheMostAllocatingZoneFirst) {
    {
        BASE_PROFILE_SCOPE("allocs.small");
        allocate_and_free(10);
    }
    {
        BASE_PROFILE_SCOPE("allocs.large");
        allocate_and_free(1000);
    }
    const std::string report = report_text();
    ASSERT_NE(report.find("allocs.large"), std::string::npos) << report;
    EXPECT_LT(report.find("allocs.large"), report.find("allocs.small")) << report;
}

TEST_F(ProfileAllocationsTest, ReportWithBothKindsShowsBothTables) {
    // Only possible by hand: a build's macros create zones of one kind.
    static base::profile::Anchor timeAnchor{"allocs.time-zone"};
    {
        const base::profile::Block block(timeAnchor);
    }
    {
        BASE_PROFILE_SCOPE("allocs.next-to-time");
        allocate_and_free(8);
    }
    const std::string report = report_text();
    EXPECT_NE(report.find("Total:"), std::string::npos) << report;
    EXPECT_NE(report.find("Allocations in zones:"), std::string::npos) << report;
    EXPECT_LT(report.find("allocs.time-zone"), report.find("Allocations in zones:")) << report;
}
