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

struct MapInsertLookup {
    using Container = MapKind;
    static constexpr std::string_view name = "insert_lookup";
    static constexpr std::string_view tags = "allocation";
    static constexpr std::string_view operation = "one insert or one lookup";
    static constexpr std::string_view description =
        "Inserts n unique keys in random order, then looks each one up in another random order. One allocation "
        "per element, where an arena's pointer bump should beat a general-purpose heap most clearly.";
    static constexpr i64 maxElements = defaultMaxElements;

    template <typename Implementation>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const u64> keys = shuffled_keys(count, 0);
        const std::span<const u64> lookups = shuffled_keys(count, 1);
        run_workload<Implementation>(
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
    using Container = SetKind;
    static constexpr std::string_view name = "insert_contains";
    static constexpr std::string_view tags = "allocation";
    static constexpr std::string_view operation = "one insert or one contains";
    static constexpr std::string_view description =
        "Inserts n unique keys in random order, then checks each one in another random order.";
    static constexpr i64 maxElements = defaultMaxElements;

    template <typename Implementation>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const u64> keys = shuffled_keys(count, 0);
        const std::span<const u64> lookups = shuffled_keys(count, 1);
        run_workload<Implementation>(
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

std::vector<BenchmarkSpec> map_benchmarks() {
    return concat(
        {specs_for<MapInsertLookup>(AllImplementations{}), specs_for<SetInsertContains>(AllImplementations{})});
}

} // namespace base::bench
