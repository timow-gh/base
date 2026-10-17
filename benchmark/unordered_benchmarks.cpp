#include "harness.hpp"
#include "synthetic_data.hpp"
#include "workloads.hpp"

#include "base/core/types.hpp"

#include <benchmark/benchmark.h>

#include <span>
#include <string_view>

namespace base::bench {

namespace {

constexpr f64 lookupHitRatio = 0.9;

// Hash tables: one node per element plus a bucket array that is replaced on every
// rehash. Without reserve the arena keeps every outgrown bucket array.
template <bool reserved>
struct UnorderedMapInsertLookup {
    static constexpr std::string_view name =
        reserved ? "unordered_map_insert_lookup_reserved" : "unordered_map_insert_lookup";
    static constexpr i64 maxElements = defaultMaxElements;

    template <typename Variant>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const u64> keys = uniform_keys(count);
        const std::span<const u64> lookups = lookup_keys(count, count, lookupHitRatio);
        run_workload<Variant>(
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
    static constexpr std::string_view name = "unordered_set_insert_contains";
    static constexpr i64 maxElements = defaultMaxElements;

    template <typename Variant>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const u64> keys = uniform_keys(count);
        const std::span<const u64> lookups = lookup_keys(count, count, lookupHitRatio);
        run_workload<Variant>(
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

void register_unordered_benchmarks() {
    register_workload<UnorderedMapInsertLookup<false>>(AllVariants{});
    register_workload<UnorderedMapInsertLookup<true>>(AllVariants{});
    register_workload<UnorderedSetInsertContains>(AllVariants{});
}

} // namespace base::bench
