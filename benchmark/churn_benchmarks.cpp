#include "harness.hpp"
#include "synthetic_data.hpp"
#include "workloads.hpp"

#include "base/core/types.hpp"

#include <benchmark/benchmark.h>

#include <span>
#include <string_view>
#include <vector>
#include <type_traits>

namespace base::bench {

namespace {

/**
 * \brief A map that keeps `elements` entries while it erases the oldest key and inserts
 * a new one, 8 × `elements` times: a sliding window, the pattern ArenaAllocator
 * documents as unsuitable.
 *
 * std::allocator reuses the freed nodes, so its footprint stays flat; the arena and pmr
 * implementations never reuse them and grow with every operation. The checkpoints after the
 * fill and after every `elements` operations show the growth.
 */
template <bool ordered>
struct MapChurn {
    using Container = std::conditional_t<ordered, MapKind, UnorderedMapKind>;
    static constexpr std::string_view name = "churn";
    static constexpr std::string_view tags = "churn";
    static constexpr std::string_view operation = "one erase plus one insert";
    static constexpr std::string_view description =
        "A container that holds n entries erases its oldest key and inserts a new one, 8n times. std::allocator "
        "reuses the freed nodes; the arena and pmr never do, so their memory grows with every operation.";
    // Each iteration touches 9 × elements keys.
    static constexpr i64 maxElements = i64{1} << 17;
    static constexpr usize operationsPerElement = 8;

    template <typename Implementation>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const u64> keys = uniform_keys(count * (operationsPerElement + 1U));
        run_workload<Implementation>(
            state,
            {.elements = count, .operations = count * operationsPerElement},
            [keys, count]<typename Containers>(auto&& source, auto&& checkpoint) {
                using Map = std::conditional_t<ordered,
                                               typename Containers::template Map<u64, u64>,
                                               typename Containers::template UnorderedMap<u64, u64>>;
                Map map(source);
                for (usize i = 0; i < count; ++i) {
                    map.emplace(keys[i], i);
                }
                checkpoint();
                for (usize i = count; i < keys.size(); ++i) {
                    map.erase(keys[i - count]);
                    map.emplace(keys[i], i);
                    if ((i + 1U) % count == 0U) {
                        checkpoint();
                    }
                }
                benchmark::DoNotOptimize(map.size());
            });
    }
};

} // namespace

std::vector<BenchmarkSpec> churn_benchmarks() {
    return concat(
        {specs_for<MapChurn<true>>(AllImplementations{}), specs_for<MapChurn<false>>(AllImplementations{})});
}

} // namespace base::bench
