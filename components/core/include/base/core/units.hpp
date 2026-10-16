#pragma once

#include <cstddef>

namespace base {

inline constexpr std::size_t bytesPerKib = 1024U;

/**
 * \brief Byte counts in binary units, e.g. `kib(64)` is 65536.
 */
[[nodiscard]] constexpr std::size_t kib(std::size_t count) noexcept {
    return count * bytesPerKib;
}

[[nodiscard]] constexpr std::size_t mib(std::size_t count) noexcept {
    return kib(count) * bytesPerKib;
}

[[nodiscard]] constexpr std::size_t gib(std::size_t count) noexcept {
    return mib(count) * bytesPerKib;
}

} // namespace base
