#include "base/core/units.hpp"

#include <gtest/gtest.h>

TEST(Units, BinaryMultiples) {
    static_assert(base::kib(1) == 1024U);
    static_assert(base::mib(1) == 1024U * 1024U);
    static_assert(base::gib(64) == std::size_t{64} * 1024U * 1024U * 1024U);
    EXPECT_EQ(base::kib(64), 65536U);
}
