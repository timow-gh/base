#include "base/core/types.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

// The aliases must be the standard types themselves, not merely the same size, so that
// they mix with the standard library without conversions.
static_assert(std::is_same_v<base::u8, std::uint8_t>);
static_assert(std::is_same_v<base::u16, std::uint16_t>);
static_assert(std::is_same_v<base::u32, std::uint32_t>);
static_assert(std::is_same_v<base::u64, std::uint64_t>);
static_assert(std::is_same_v<base::i8, std::int8_t>);
static_assert(std::is_same_v<base::i16, std::int16_t>);
static_assert(std::is_same_v<base::i32, std::int32_t>);
static_assert(std::is_same_v<base::i64, std::int64_t>);
static_assert(std::is_same_v<base::usize, std::size_t>);
static_assert(std::is_same_v<base::isize, std::ptrdiff_t>);
static_assert(std::is_same_v<base::uptr, std::uintptr_t>);
static_assert(std::is_same_v<base::f32, float>);
static_assert(std::is_same_v<base::f64, double>);

using namespace base::types;

TEST(Types, UsableOutsideBaseThroughTypesNamespace) {
    constexpr usize maxSize = std::numeric_limits<usize>::max();
    EXPECT_EQ(maxSize, std::numeric_limits<std::size_t>::max());
    EXPECT_EQ(std::numeric_limits<u8>::max(), 255U);
    EXPECT_EQ(std::numeric_limits<i8>::min(), -128);
}
