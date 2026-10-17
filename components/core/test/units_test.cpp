#include "base/core/units.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <source_location>
#include <string_view>

namespace {

int& failed_assertions() {
    static int count = 0;
    return count;
}

void counting_handler([[maybe_unused]] std::string_view expression,
                      [[maybe_unused]] std::string_view message,
                      [[maybe_unused]] const std::source_location& location) {
    ++failed_assertions();
}

class UnitsTest : public ::testing::Test {
protected:
    void SetUp() override {
        failed_assertions() = 0;
        m_previous = base::set_assert_handler(&counting_handler);
    }

    void TearDown() override { base::set_assert_handler(m_previous); }

private:
    base::AssertHandler m_previous = nullptr;
};

} // namespace

TEST(Units, BinaryMultiples) {
    static_assert(base::kib(1) == 1024U);
    static_assert(base::mib(1) == 1024U * 1024U);
    static_assert(base::gib(64) == std::size_t{64} * 1024U * 1024U * 1024U);
    EXPECT_EQ(base::kib(64), 65536U);
}

TEST_F(UnitsTest, LargestCountsDoNotAssert) {
    constexpr std::size_t maxBytes = std::numeric_limits<std::size_t>::max();
    EXPECT_EQ(base::kib(maxBytes / 1024U), (maxBytes / 1024U) * 1024U);
    EXPECT_EQ(base::gib(maxBytes / base::gib(1)), (maxBytes / base::gib(1)) * base::gib(1));
    EXPECT_EQ(failed_assertions(), 0);
}

TEST_F(UnitsTest, OverflowAsserts) {
#if BASE_ENABLE_ASSERTS
    constexpr std::size_t maxBytes = std::numeric_limits<std::size_t>::max();
    [[maybe_unused]] const std::size_t kibOverflow = base::kib((maxBytes / 1024U) + 1U);
    EXPECT_EQ(failed_assertions(), 1);
    // Fits in KiB and MiB, overflows only in the last step to GiB.
    [[maybe_unused]] const std::size_t gibOverflow = base::gib((maxBytes / base::gib(1)) + 1U);
    EXPECT_EQ(failed_assertions(), 2);
#else
    GTEST_SKIP() << "assertions are disabled";
#endif
}
