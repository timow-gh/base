#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

/**
 * \brief Short names for the fixed-width integer, size and floating-point types
 *
 * Plain aliases, not distinct types: `usize` *is* `std::size_t`, so they mix freely with
 * the standard library. The inline namespace lets code outside `base` (tests, consumers)
 * pull in just these names with `using namespace base::types;` instead of all of `base`.
 *
 * Use std::byte, not u8, for raw memory: u8 is arithmetic, std::byte is storage.
 */
namespace base::inline types {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

// Sizes, counts and indices into memory.
using usize = std::size_t;
// Signed sizes, indices and pointer differences.
using isize = std::ptrdiff_t;
// Addresses as integers, e.g. for alignment checks.
using uptr = std::uintptr_t;

// Unlike <cstdint> for integers, C++ does not fix the format of float and double; the
// assertions make the names true on every supported platform.
using f32 = float;
using f64 = double;
static_assert(sizeof(f32) == sizeof(u32) && std::numeric_limits<f32>::is_iec559, "f32 must be IEEE 754 binary32");
static_assert(sizeof(f64) == sizeof(u64) && std::numeric_limits<f64>::is_iec559, "f64 must be IEEE 754 binary64");

// Character types get no aliases: their names are already short, and what they encode
// matters more than their width, which an alias like `c8` would hide. Which one to use:
// - char: all text, including UTF-8. String literals, std::string, std::string_view,
//   std::format and C APIs all take it.
// - char8_t: only where an API demands it. It does not mix with std::string or
//   std::format, so convert to char at the boundary.
// - wchar_t: only for wide Windows APIs, e.g. _wfopen_s with
//   std::filesystem::path::c_str().
// - char16_t, char32_t: only for data that is UTF-16 or UTF-32 by format.
// - signed char, unsigned char: not for text. Use i8/u8 for small numbers and std::byte
//   for raw memory.

} // namespace base::inline types
