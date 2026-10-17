#pragma once

#include "base/core/assert.hpp"

#include <cstddef>
#include <limits>

namespace base {

inline constexpr std::size_t bytesPerKib = 1024U;

/**
 * \brief Byte count of `count` kibibytes, e.g. `kib(64)` is 65536.
 *
 * KiB, MiB and GiB are the IEC binary units: 1 KiB = 1024 bytes, 1 MiB = 1024 KiB.
 * KB, MB and GB are the SI decimal units: 1 KB = 1000 bytes, 1 MB = 1000 KB. Use the
 * binary units for memory: RAM, pages, address space and alignment all come in
 * powers of two. The decimal units belong to disk capacities, network throughput and
 * file sizes as some operating systems report them. Windows and JEDEC write "KB" for
 * 1024 bytes, which is why the unambiguous IEC names exist.
 *
 * There are deliberately no decimal helpers: this library only sizes memory, and a
 * `kb()` next to `kib()` would turn a typo into a size that compiles and is off by
 * 2.4% (KB) to 7.4% (GB).
 *
 * Asserts that the result fits in std::size_t, which makes an overflow in a constant
 * expression a compile error.
 */
[[nodiscard]] constexpr std::size_t kib(std::size_t count) noexcept {
    BASE_ASSERT_MSG(count <= std::numeric_limits<std::size_t>::max() / bytesPerKib, "byte count overflows std::size_t");
    return count * bytesPerKib;
}

/** \brief Byte count of `count` mebibytes (1024 KiB), see kib(). */
[[nodiscard]] constexpr std::size_t mib(std::size_t count) noexcept {
    return kib(kib(count));
}

/** \brief Byte count of `count` gibibytes (1024 MiB), see kib(). */
[[nodiscard]] constexpr std::size_t gib(std::size_t count) noexcept {
    return kib(mib(count));
}

} // namespace base
