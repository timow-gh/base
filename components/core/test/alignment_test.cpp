#include "base/core/alignment.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>

TEST(Alignment, IsPowerOfTwo) {
    EXPECT_FALSE(base::is_power_of_two(0U));
    EXPECT_TRUE(base::is_power_of_two(1U));
    EXPECT_TRUE(base::is_power_of_two(2U));
    EXPECT_FALSE(base::is_power_of_two(3U));
    EXPECT_TRUE(base::is_power_of_two(std::size_t{4096}));
    EXPECT_FALSE(base::is_power_of_two(std::size_t{4097}));
    EXPECT_TRUE(base::is_power_of_two(std::uint64_t{1} << 63U));
    EXPECT_FALSE(base::is_power_of_two(std::numeric_limits<std::uint64_t>::max()));
}

TEST(Alignment, AlignUp) {
    using Size = std::size_t;
    EXPECT_EQ(base::align_up(Size{0}, Size{16}), 0U);
    EXPECT_EQ(base::align_up(Size{1}, Size{16}), 16U);
    EXPECT_EQ(base::align_up(Size{16}, Size{16}), 16U);
    EXPECT_EQ(base::align_up(Size{17}, Size{16}), 32U);
    EXPECT_EQ(base::align_up(Size{5}, Size{1}), 5U);
    EXPECT_EQ(base::align_up(Size{4095}, Size{4096}), 4096U);
}

TEST(Alignment, AlignUpSmallTypesDoNotPromote) {
    EXPECT_EQ(base::align_up(std::uint8_t{3}, std::uint8_t{4}), std::uint8_t{4});
    EXPECT_EQ(base::align_up(std::uint8_t{252}, std::uint8_t{4}), std::uint8_t{252});
}

TEST(Alignment, AlignDown) {
    using Size = std::size_t;
    EXPECT_EQ(base::align_down(Size{0}, Size{16}), 0U);
    EXPECT_EQ(base::align_down(Size{15}, Size{16}), 0U);
    EXPECT_EQ(base::align_down(Size{16}, Size{16}), 16U);
    EXPECT_EQ(base::align_down(Size{31}, Size{16}), 16U);
}

TEST(Alignment, IsAligned) {
    using Size = std::size_t;
    EXPECT_TRUE(base::is_aligned(Size{0}, Size{8}));
    EXPECT_TRUE(base::is_aligned(Size{64}, Size{8}));
    EXPECT_FALSE(base::is_aligned(Size{65}, Size{8}));
    EXPECT_TRUE(base::is_aligned(Size{65}, Size{1}));
}

TEST(Alignment, UsableInConstantExpressions) {
    static_assert(base::align_up(std::size_t{100}, std::size_t{64}) == 128U);
    static_assert(base::align_down(std::size_t{100}, std::size_t{64}) == 64U);
    static_assert(base::is_aligned(std::size_t{128}, std::size_t{64}));
    static_assert(base::is_power_of_two(64U));
}
