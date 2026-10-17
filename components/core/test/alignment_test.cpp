#include "base/core/alignment.hpp"

#include "base/core/types.hpp"

#include <gtest/gtest.h>

#include <limits>

using namespace base::types;

TEST(Alignment, IsPowerOfTwo) {
    EXPECT_FALSE(base::is_power_of_two(0U));
    EXPECT_TRUE(base::is_power_of_two(1U));
    EXPECT_TRUE(base::is_power_of_two(2U));
    EXPECT_FALSE(base::is_power_of_two(3U));
    EXPECT_TRUE(base::is_power_of_two(usize{4096}));
    EXPECT_FALSE(base::is_power_of_two(usize{4097}));
    EXPECT_TRUE(base::is_power_of_two(u64{1} << 63U));
    EXPECT_FALSE(base::is_power_of_two(std::numeric_limits<u64>::max()));
}

TEST(Alignment, AlignUp) {
    EXPECT_EQ(base::align_up(usize{0}, usize{16}), 0U);
    EXPECT_EQ(base::align_up(usize{1}, usize{16}), 16U);
    EXPECT_EQ(base::align_up(usize{16}, usize{16}), 16U);
    EXPECT_EQ(base::align_up(usize{17}, usize{16}), 32U);
    EXPECT_EQ(base::align_up(usize{5}, usize{1}), 5U);
    EXPECT_EQ(base::align_up(usize{4095}, usize{4096}), 4096U);
}

TEST(Alignment, AlignUpSmallTypesDoNotPromote) {
    EXPECT_EQ(base::align_up(u8{3}, u8{4}), u8{4});
    EXPECT_EQ(base::align_up(u8{252}, u8{4}), u8{252});
}

TEST(Alignment, AlignDown) {
    EXPECT_EQ(base::align_down(usize{0}, usize{16}), 0U);
    EXPECT_EQ(base::align_down(usize{15}, usize{16}), 0U);
    EXPECT_EQ(base::align_down(usize{16}, usize{16}), 16U);
    EXPECT_EQ(base::align_down(usize{31}, usize{16}), 16U);
}

TEST(Alignment, IsAligned) {
    EXPECT_TRUE(base::is_aligned(usize{0}, usize{8}));
    EXPECT_TRUE(base::is_aligned(usize{64}, usize{8}));
    EXPECT_FALSE(base::is_aligned(usize{65}, usize{8}));
    EXPECT_TRUE(base::is_aligned(usize{65}, usize{1}));
}

TEST(Alignment, UsableInConstantExpressions) {
    static_assert(base::align_up(usize{100}, usize{64}) == 128U);
    static_assert(base::align_down(usize{100}, usize{64}) == 64U);
    static_assert(base::is_aligned(usize{128}, usize{64}));
    static_assert(base::is_power_of_two(64U));
}
