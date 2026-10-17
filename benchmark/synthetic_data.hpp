#pragma once

#include "base/core/types.hpp"

#include <span>
#include <string>
#include <vector>

namespace base::bench {

// Keys 0, 1, 2, ...
[[nodiscard]] std::span<const u64> sequential_keys(usize count);

/**
 * \brief Uniformly random keys with the lowest bit cleared. Collisions are possible
 * in theory (birthday bound ~count² / 2^64) and irrelevant in practice.
 *
 * The cleared bit leaves room for keys that are guaranteed misses, see lookup_keys().
 */
[[nodiscard]] std::span<const u64> uniform_keys(usize count);

/**
 * \brief A random permutation of 0 .. count - 1: unique keys in random insertion order.
 * \param stream Selects an independent permutation, e.g. one order to insert and
 * another to look up.
 */
[[nodiscard]] std::span<const u64> shuffled_keys(usize count, u32 stream = 0);

/**
 * \brief `lookupCount` lookups into uniform_keys(keyCount): a fraction `hitRatio` hits,
 * the rest guaranteed misses (lowest bit set).
 *
 * Hits follow a Zipf distribution over the keys, as in caches and symbol tables: the
 * key of rank r is looked up with weight 1 / r^exponent, so a few hot keys dominate.
 * O(keyCount + lookupCount · log keyCount) to generate.
 */
[[nodiscard]] std::span<const u64> lookup_keys(usize keyCount, usize lookupCount, f64 hitRatio,
                                               f64 exponent = 1.0);

enum class StringLength : u8 {
    Short, // 4 to 15 characters: fits the small-string buffer of every standard library.
    Long,  // 32 to 128 characters: always allocates.
};

// `count` random alphanumeric strings.
[[nodiscard]] std::span<const std::string> random_strings(usize count, StringLength length);

} // namespace base::bench
