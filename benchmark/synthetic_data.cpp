#include "synthetic_data.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <string_view>
#include <tuple>
#include <utility>

namespace base::bench {

namespace {

// Distinct base seeds, so generators with the same count do not share a sequence.
constexpr u64 uniformSeed = 0x9E3779B97F4A7C15ULL;
constexpr u64 shuffleSeed = 0xC2B2AE3D27D4EB4FULL;
constexpr u64 lookupSeed = 0x165667B19E3779F9ULL;
constexpr u64 stringSeed = 0x27D4EB2F165667C5ULL;

// A fixed seed per (generator, count, stream), so every variant and every run sees the
// same data. Only the engine's raw output is used, which the standard fixes:
// std::uniform_int_distribution and std::shuffle differ between standard libraries and
// would give each compiler different data.
[[nodiscard]] std::mt19937_64 make_engine(u64 baseSeed, usize count, u64 stream = 0) {
    return std::mt19937_64(baseSeed ^ (static_cast<u64>(count) * 0xFF51AFD7ED558CCDULL) ^ (stream << 32U));
}

// `engine() % bound` is biased by at most bound / 2^64, far below anything measurable.
[[nodiscard]] u64 below(std::mt19937_64& engine, u64 bound) {
    return engine() % bound;
}

// A double in [0, 1) from the top 53 bits.
[[nodiscard]] f64 unit_interval(std::mt19937_64& engine) {
    constexpr f64 scale = 1.0 / static_cast<f64>(1ULL << 53U);
    return static_cast<f64>(engine() >> 11U) * scale;
}

template <typename T>
void fisher_yates(std::vector<T>& values, std::mt19937_64& engine) {
    for (usize i = values.size(); i > 1U; --i) {
        const usize j = below(engine, i);
        std::swap(values[i - 1U], values[j]);
    }
}

// Google Benchmark calls a benchmark function once per repetition, and regenerating a
// million keys each time would only slow the run down. Single-threaded, like the
// benchmarks.
template <typename Key, typename Value, typename Generate>
[[nodiscard]] const std::vector<Value>& cached(std::map<Key, std::vector<Value>>& cache, const Key& key,
                                               Generate generate) {
    auto found = cache.find(key);
    if (found == cache.end()) {
        found = cache.emplace(key, generate()).first;
    }
    return found->second;
}

} // namespace

std::span<const u64> sequential_keys(usize count) {
    static std::map<usize, std::vector<u64>> cache;
    return cached(cache, count, [count] {
        std::vector<u64> keys(count);
        for (usize i = 0; i < count; ++i) {
            keys[i] = i;
        }
        return keys;
    });
}

std::span<const u64> uniform_keys(usize count) {
    static std::map<usize, std::vector<u64>> cache;
    return cached(cache, count, [count] {
        std::mt19937_64 engine = make_engine(uniformSeed, count);
        std::vector<u64> keys(count);
        for (u64& key : keys) {
            key = engine() & ~1ULL;
        }
        return keys;
    });
}

std::span<const u64> shuffled_keys(usize count, u32 stream) {
    static std::map<std::pair<usize, u32>, std::vector<u64>> cache;
    return cached(cache, std::pair{count, stream}, [count, stream] {
        std::vector<u64> keys(sequential_keys(count).begin(), sequential_keys(count).end());
        std::mt19937_64 engine = make_engine(shuffleSeed, count, stream);
        fisher_yates(keys, engine);
        return keys;
    });
}

std::span<const u64> lookup_keys(usize keyCount, usize lookupCount, f64 hitRatio, f64 exponent) {
    using CacheKey = std::tuple<usize, usize, f64, f64>;
    static std::map<CacheKey, std::vector<u64>> cache;
    return cached(cache, CacheKey{keyCount, lookupCount, hitRatio, exponent}, [=] {
        const std::span<const u64> keys = uniform_keys(keyCount);

        // Cumulative Zipf weights over the ranks; rank r maps to keys[r], and keys are in
        // random order already, so the hot keys are spread over the container.
        std::vector<f64> cumulative(keyCount);
        f64 total = 0.0;
        for (usize rank = 0; rank < keyCount; ++rank) {
            total += 1.0 / std::pow(static_cast<f64>(rank + 1U), exponent);
            cumulative[rank] = total;
        }

        std::mt19937_64 engine = make_engine(lookupSeed, keyCount, lookupCount);
        std::vector<u64> lookups(lookupCount);
        for (u64& lookup : lookups) {
            if (unit_interval(engine) < hitRatio) {
                const f64 target = unit_interval(engine) * total;
                const auto rank = static_cast<usize>(std::ranges::upper_bound(cumulative, target) - cumulative.begin());
                lookup = keys[std::min(rank, keyCount - 1U)];
            } else {
                lookup = engine() | 1ULL;
            }
        }
        return lookups;
    });
}

std::span<const std::string> random_strings(usize count, StringLength length) {
    static std::map<std::pair<usize, StringLength>, std::vector<std::string>> cache;
    return cached(cache, std::pair{count, length}, [count, length] {
        constexpr std::string_view alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        const auto [minLength, maxLength] = length == StringLength::Short ? std::pair<usize, usize>{4, 15}
                                                                           : std::pair<usize, usize>{32, 128};
        std::mt19937_64 engine = make_engine(stringSeed, count, static_cast<u64>(length));
        std::vector<std::string> strings(count);
        for (std::string& text : strings) {
            text.resize(minLength + below(engine, maxLength - minLength + 1U));
            for (char& character : text) {
                character = alphabet[below(engine, alphabet.size())];
            }
        }
        return strings;
    });
}

} // namespace base::bench
