#pragma once

#include "base/core/assert.hpp"

#include <bit>
#include <concepts>

namespace base {

/**
 * \brief Returns true if `value` is a power of two. Zero is not a power of two.
 */
template <std::unsigned_integral T>
[[nodiscard]] constexpr bool is_power_of_two(T value) noexcept {
    return std::has_single_bit(value);
}

/**
 * \brief Rounds `value` up to the next multiple of `alignment`.
 * \param alignment A power of two.
 *
 * The caller guarantees that the result is representable in T; use it on sizes and
 * offsets that are already bounds-checked.
 */
template <std::unsigned_integral T>
[[nodiscard]] constexpr T align_up(T value, T alignment) noexcept {
    BASE_ASSERT(is_power_of_two(alignment), "alignment must be a power of two");
    const T mask = static_cast<T>(alignment - 1U);
    return static_cast<T>((value + mask) & static_cast<T>(~mask));
}

/**
 * \brief Rounds `value` down to the previous multiple of `alignment`.
 * \param alignment A power of two.
 */
template <std::unsigned_integral T>
[[nodiscard]] constexpr T align_down(T value, T alignment) noexcept {
    BASE_ASSERT(is_power_of_two(alignment), "alignment must be a power of two");
    const T mask = static_cast<T>(alignment - 1U);
    return static_cast<T>(value & static_cast<T>(~mask));
}

/**
 * \brief Returns true if `value` is a multiple of `alignment`.
 * \param alignment A power of two.
 */
template <std::unsigned_integral T>
[[nodiscard]] constexpr bool is_aligned(T value, T alignment) noexcept {
    BASE_ASSERT(is_power_of_two(alignment), "alignment must be a power of two");
    return (value & static_cast<T>(alignment - 1U)) == 0U;
}

} // namespace base
