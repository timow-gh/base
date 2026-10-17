// Enabled regardless of base_PROFILING. Only the macros depend on the switch, so the
// enabled and disabled test files can share one binary.
#undef BASE_ENABLE_PROFILING
#define BASE_ENABLE_PROFILING 1

#include "base/core/types.hpp"
#include "base/profile/profile.hpp"

#include "exit_test_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

using base::profile::ZoneStats;
using namespace base::types;

namespace {

// Gives zones measurable work without depending on how long it takes.
void spin() {
    volatile u64 sink = 0;
    for (u64 i = 0; i < 1000U; ++i) {
        sink = sink + i;
    }
}

void hit_zone() {
    BASE_PROFILE_SCOPE("test.hits");
    spin();
}

void process_bytes(u64 byteCount) {
    BASE_PROFILE_BANDWIDTH("test.bandwidth", byteCount);
    spin();
}

int recurse(int depth) {
    BASE_PROFILE_SCOPE("test.recursion");
    spin();
    return depth == 0 ? 0 : 1 + recurse(depth - 1);
}

// Each instantiation is a zone of its own, see Anchor.
template <usize Index>
void cheap_zone() {
    BASE_PROFILE_SCOPE("test.cheap");
}

template <usize... Indices>
void enter_cheap_zones(std::index_sequence<Indices...> /*indices*/) {
    (cheap_zone<Indices>(), ...);
}

[[nodiscard]] ZoneStats stats_of(std::string_view name) {
    const std::optional<ZoneStats> stats = base::profile::zone_stats(name);
    EXPECT_TRUE(stats.has_value()) << name;
    return stats.value_or(ZoneStats{});
}

[[nodiscard]] std::string read_all(std::FILE* file) {
    std::rewind(file);
    std::string text;
    std::array<char, 256> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), file) != nullptr) {
        text += buffer.data();
    }
    return text;
}

class ProfileTest : public ::testing::Test {
protected:
    void SetUp() override { base::profile::reset(); }
};

} // namespace

TEST_F(ProfileTest, CountsEveryPassThroughAZone) {
    for (int i = 0; i < 3; ++i) {
        hit_zone();
    }
    EXPECT_EQ(stats_of("test.hits").hitCount, 3U);
}

TEST_F(ProfileTest, ResetResetsTheZones) {
    hit_zone();
    hit_zone();
    base::profile::reset();
    hit_zone();
    EXPECT_EQ(stats_of("test.hits").hitCount, 1U);
}

TEST_F(ProfileTest, BandwidthZonesAddUpTheProcessedBytes) {
    process_bytes(100);
    process_bytes(28);
    const ZoneStats stats = stats_of("test.bandwidth");
    EXPECT_EQ(stats.hitCount, 2U);
    EXPECT_EQ(stats.processedBytes, 128U);
}

TEST_F(ProfileTest, NestedZoneTimeIsExcludedFromTheParent) {
    {
        BASE_PROFILE_SCOPE("test.outer");
        spin();
        {
            BASE_PROFILE_SCOPE("test.inner");
            spin();
        }
        spin();
    }
    const ZoneStats outer = stats_of("test.outer");
    const ZoneStats inner = stats_of("test.inner");
    EXPECT_EQ(outer.inclusiveTicks, outer.exclusiveTicks + inner.inclusiveTicks);
    EXPECT_EQ(inner.exclusiveTicks, inner.inclusiveTicks);
}

TEST_F(ProfileTest, RecursiveZoneCountsItsTimeOnce) {
    EXPECT_EQ(recurse(5), 5);
    const ZoneStats stats = stats_of("test.recursion");
    EXPECT_EQ(stats.hitCount, 6U);
    // All time is spent inside this one zone, so nothing is excluded, and the
    // inclusive time is that of the outermost call, not the sum of all six.
    EXPECT_EQ(stats.exclusiveTicks, stats.inclusiveTicks);
}

TEST_F(ProfileTest, ZoneStatsOfAnUnknownZoneIsEmpty) {
    EXPECT_FALSE(base::profile::zone_stats("test.never-entered").has_value());
}

TEST_F(ProfileTest, ReportListsTheEnteredZones) {
    hit_zone();
    process_bytes(1024);

    std::FILE* file = std::tmpfile();
    ASSERT_NE(file, nullptr);
    base::profile::print_report(file);
    const std::string report = read_all(file);
    std::fclose(file);

    EXPECT_NE(report.find("Total:"), std::string::npos) << report;
    EXPECT_NE(report.find("test.hits"), std::string::npos) << report;
    EXPECT_NE(report.find("test.bandwidth"), std::string::npos) << report;
    EXPECT_NE(report.find("GB/s"), std::string::npos) << report;
    // Zones without hits since reset() are left out.
    EXPECT_EQ(report.find("test.recursion"), std::string::npos) << report;
}

TEST_F(ProfileTest, ReportKeepsTheMostExpensiveZonesWhenThereAreTooMany) {
    // The report shows at most 512 zones and collects them newest-registered first.
    // Registering one expensive zone before and one after 600 cheap ones means the
    // older expensive zone arrives when the report is already full; it must evict a
    // cheap zone, not the other expensive one.
    {
        BASE_PROFILE_SCOPE("test.expensive.registered-first");
        for (int i = 0; i < 200; ++i) {
            spin();
        }
    }
    enter_cheap_zones(std::make_index_sequence<600>{});
    {
        BASE_PROFILE_SCOPE("test.expensive.registered-last");
        for (int i = 0; i < 100; ++i) {
            spin();
        }
    }

    std::FILE* file = std::tmpfile();
    ASSERT_NE(file, nullptr);
    base::profile::print_report(file);
    const std::string report = read_all(file);
    std::fclose(file);

    EXPECT_NE(report.find("test.expensive.registered-first"), std::string::npos);
    EXPECT_NE(report.find("test.expensive.registered-last"), std::string::npos);
    EXPECT_NE(report.find("90 more zones not shown"), std::string::npos);
}

// Each EXPECT_EXIT runs its statement in a fresh child process, so the session there
// starts with the first zone, as in a program that never calls reset().
TEST(ProfileExitDeathTest, ReportGoesToStdoutAtExitByDefault) {
    if (!std::string_view(BASE_PROFILE_REPORT_DIRECTORY).empty()) {
        GTEST_SKIP() << "base was configured with base_PROFILING_REPORT_DIRECTORY";
    }
    const std::filesystem::path capture = exit_test::fresh_scratch_directory("stdout-default") / "stdout.txt";
    EXPECT_EXIT(
        {
            exit_test::redirect_stdout_to(capture);
            {
                BASE_PROFILE_SCOPE("exit.default-report");
            }
            std::exit(0);
        },
        ::testing::ExitedWithCode(0),
        "");
    const std::string output = exit_test::read_file(capture);
    EXPECT_NE(output.find("Total:"), std::string::npos) << output;
    EXPECT_NE(output.find("exit.default-report"), std::string::npos) << output;
    std::filesystem::remove_all(capture.parent_path());
}

TEST(ProfileExitDeathTest, PrintReportTurnsTheExitReportOff) {
    const std::filesystem::path capture = exit_test::fresh_scratch_directory("stdout-reported") / "stdout.txt";
    EXPECT_EXIT(
        {
            exit_test::redirect_stdout_to(capture);
            {
                BASE_PROFILE_SCOPE("exit.reported");
            }
            std::FILE* file = std::tmpfile();
            base::profile::print_report(file != nullptr ? file : stderr);
            std::exit(0);
        },
        ::testing::ExitedWithCode(0),
        "");
    EXPECT_EQ(exit_test::read_file(capture), "");
    std::filesystem::remove_all(capture.parent_path());
}

TEST(ProfileExitDeathTest, ExitInsideAZoneSkipsTheReport) {
    const std::filesystem::path capture = exit_test::fresh_scratch_directory("stdout-inside-zone") / "stdout.txt";
    EXPECT_EXIT(
        {
            exit_test::redirect_stdout_to(capture);
            BASE_PROFILE_SCOPE("exit.inside-zone");
            std::exit(0);
        },
        ::testing::ExitedWithCode(0),
        "");
    const std::string output = exit_test::read_file(capture);
    EXPECT_NE(output.find("exited inside a zone"), std::string::npos) << output;
    EXPECT_EQ(output.find("Total:"), std::string::npos) << output;
    std::filesystem::remove_all(capture.parent_path());
}
