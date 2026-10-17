#include "harness.hpp"
#include "synthetic_data.hpp"
#include "workloads.hpp"

#include "base/core/types.hpp"

#include <benchmark/benchmark.h>

#include <span>
#include <string_view>

namespace base::bench {

namespace {

// Ordered node containers: one allocation per element, which is where an arena's
// pointer bump should beat a general-purpose heap most clearly.
struct MapInsertLookup {
    static constexpr std::string_view name = "map_insert_lookup";
    static constexpr i64 maxElements = defaultMaxElements;

    template <typename Variant>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const u64> keys = shuffled_keys(count, 0);
        const std::span<const u64> lookups = shuffled_keys(count, 1);
        run_workload<Variant>(
            state,
            {.elements = count, .operations = 2U * count},
            [keys, lookups]<typename Containers>(auto&& source, [[maybe_unused]] auto&& checkpoint) {
                typename Containers::template Map<u64, u64> map(source);
                for (const u64 key : keys) {
                    map.emplace(key, key);
                }
                u64 sum = 0;
                for (const u64 key : lookups) {
                    sum += map.find(key)->second;
                }
                benchmark::DoNotOptimize(sum);
            });
    }
};

struct SetInsertContains {
    static constexpr std::string_view name = "set_insert_contains";
    static constexpr i64 maxElements = defaultMaxElements;

    template <typename Variant>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const u64> keys = shuffled_keys(count, 0);
        const std::span<const u64> lookups = shuffled_keys(count, 1);
        run_workload<Variant>(
            state,
            {.elements = count, .operations = 2U * count},
            [keys, lookups]<typename Containers>(auto&& source, [[maybe_unused]] auto&& checkpoint) {
                typename Containers::template Set<u64> set(source);
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

void register_map_benchmarks() {
    register_workload<MapInsertLookup>(AllVariants{});
    register_workload<SetInsertContains>(AllVariants{});
}

} // namespace base::bench
