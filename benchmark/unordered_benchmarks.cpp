#include "harness.hpp"
#include "synthetic_data.hpp"
#include "workloads.hpp"

#include "base/core/types.hpp"

#include <benchmark/benchmark.h>

#include <span>
#include <string_view>
#include <vector>

namespace base::bench {

namespace {

constexpr f64 lookupHitRatio = 0.9;

template <bool reserved>
struct UnorderedMapInsertLookup {
    using Container = UnorderedMapKind;
    static constexpr std::string_view name = reserved ? "insert_lookup_reserved" : "insert_lookup";
    static constexpr std::string_view tags = "allocation";
    static constexpr std::string_view operation = "one insert or one lookup";
    static constexpr std::string_view description =
        reserved ? "Inserts n random keys after reserve(n), then performs n lookups (90 % Zipf-distributed hits, "
                   "10 % misses). Without rehashing, only the node allocations differ."
                 : "Inserts n random keys without reserve, then performs n lookups: 90 % hits with Zipf-distributed "
                   "key popularity (s = 1), 10 % misses. Every rehash leaves the old bucket array behind in the "
                   "arena.";
    static constexpr i64 maxElements = defaultMaxElements;

    template <typename Implementation>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const u64> keys = uniform_keys(count);
        const std::span<const u64> lookups = lookup_keys(count, count, lookupHitRatio);
        run_workload<Implementation>(
            state,
            {.elements = count, .operations = 2U * count},
            [keys, lookups]<typename Containers>(auto&& source, [[maybe_unused]] auto&& checkpoint) {
                typename Containers::template UnorderedMap<u64, u64> map(source);
                if constexpr (reserved) {
                    map.reserve(keys.size());
                }
                for (const u64 key : keys) {
                    map.emplace(key, key);
                }
                u64 sum = 0;
                for (const u64 key : lookups) {
                    if (const auto found = map.find(key); found != map.end()) {
                        sum += found->second;
                    }
                }
                benchmark::DoNotOptimize(sum);
            });
    }
};

struct UnorderedSetInsertContains {
    using Container = UnorderedSetKind;
    static constexpr std::string_view name = "insert_contains";
    static constexpr std::string_view tags = "allocation";
    static constexpr std::string_view operation = "one insert or one contains";
    static constexpr std::string_view description =
        "Inserts n random keys without reserve, then performs n contains checks: 90 % Zipf-distributed hits, "
        "10 % misses.";
    static constexpr i64 maxElements = defaultMaxElements;

    template <typename Implementation>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const u64> keys = uniform_keys(count);
        const std::span<const u64> lookups = lookup_keys(count, count, lookupHitRatio);
        run_workload<Implementation>(
            state,
            {.elements = count, .operations = 2U * count},
            [keys, lookups]<typename Containers>(auto&& source, [[maybe_unused]] auto&& checkpoint) {
                typename Containers::template UnorderedSet<u64> set(source);
                for (const u64 key : keys) {
                    set.insert(key);
                }
                usize found = 0;
                for (const u64 key : lookups) {
                    found += set.contains(key) ? 1U : 0U;
                }
                benchmark::DoNotOptimize(found);
            });
    }
};

} // namespace

std::vector<BenchmarkSpec> unordered_benchmarks() {
    return concat({specs_for<UnorderedMapInsertLookup<false>>(AllImplementations{}),
                   specs_for<UnorderedMapInsertLookup<true>>(AllImplementations{}),
                   specs_for<UnorderedSetInsertContains>(AllImplementations{})});
}

} // namespace base::bench
