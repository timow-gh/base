#pragma once

#include "base/core/types.hpp"

#include <array>
#include <span>
#include <string_view>

namespace base::bench {

/**
 * \brief How long a run takes: which sizes, how many repetitions and how long each
 * measures. Repetitions, min time and interleaving are defaults that matching
 * --benchmark_* arguments override.
 */
struct Suite {
    std::string_view name;
    std::string_view purpose;
    std::span<const i64> sizes;
    i32 repetitions = 1;
    // In Google Benchmark's syntax, e.g. "0.05s".
    std::string_view minTime;
    // Shuffles all repetitions, so frequency or thermal drift spreads over every
    // implementation instead of biasing the one that happens to run last.
    bool interleave = false;
};

inline constexpr std::array<i64, 1> canarySizes{4096};
inline constexpr std::array<i64, 3> quickSizes{512, 32768, 262144};
inline constexpr std::array<i64, 6> fullSizes{64, 512, 4096, 32768, 262144, 1048576};

inline constexpr std::array suites{
    Suite{.name = "canary",
          .purpose = "seconds: does everything still run, and nothing is wildly off",
          .sizes = canarySizes,
          .repetitions = 1,
          .minTime = "0.01s",
          .interleave = false},
    Suite{.name = "quick",
          .purpose = "about two minutes: feedback while changing code",
          .sizes = quickSizes,
          .repetitions = 3,
          .minTime = "0.05s",
          .interleave = true},
    Suite{.name = "full",
          .purpose = "the complete picture, with tight confidence intervals",
          .sizes = fullSizes,
          .repetitions = 10,
          .minTime = "0.1s",
          .interleave = true},
};

inline constexpr std::string_view defaultSuite = "quick";

} // namespace base::bench
