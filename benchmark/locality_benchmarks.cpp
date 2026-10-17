#include "harness.hpp"
#include "synthetic_data.hpp"
#include "workloads.hpp"

#include "base/core/types.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace base::bench {

namespace {

/**
 * \brief Inserts `keys` into `map`, allocating an unrelated heap block of 16 to 256
 * bytes after each insert, as other code on a real program's heap would.
 *
 * With std::allocator the nodes end up interleaved with those blocks; the arena and
 * pmr variants place them next to each other regardless. The returned blocks must
 * stay alive while the map is measured.
 */
template <typename Map>
[[nodiscard]] std::vector<std::unique_ptr<std::byte[]>> insert_between_heap_noise(Map& map,
                                                                                  std::span<const u64> keys) {
    constexpr u64 minNoiseBytes = 16;
    constexpr u64 noiseByteRange = 241;
    std::vector<std::unique_ptr<std::byte[]>> noise;
    noise.reserve(keys.size());
    for (const u64 key : keys) {
        map.emplace(key, key);
        noise.push_back(std::make_unique_for_overwrite<std::byte[]>(minNoiseBytes + (key % noiseByteRange)));
    }
    return noise;
}

// Lookups on a map built before the timed loop: only node locality differs.
struct MapLookupPrebuilt {
    static constexpr std::string_view name = "map_lookup_prebuilt";
    static constexpr i64 maxElements = defaultMaxElements;

    template <typename Variant>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const u64> keys = shuffled_keys(count, 0);
        const std::span<const u64> lookups = shuffled_keys(count, 1);
        Variant::with_source([&]<typename Containers>(auto&& source) {
            typename Containers::template Map<u64, u64> map(source);
            const auto noise = insert_between_heap_noise(map, keys);
            for ([[maybe_unused]] auto iteration : state) {
                u64 sum = 0;
                for (const u64 key : lookups) {
                    sum += map.find(key)->second;
                }
                benchmark::DoNotOptimize(sum);
            }
        });
        state.SetItemsProcessed(state.iterations() * static_cast<i64>(count));
        state.counters["elements"] = static_cast<f64>(count);
    }
};

// A full in-order traversal: pure pointer chasing from node to node.
struct MapIteratePrebuilt {
    static constexpr std::string_view name = "map_iterate_prebuilt";
    static constexpr i64 maxElements = defaultMaxElements;

    template <typename Variant>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const u64> keys = shuffled_keys(count, 0);
        Variant::with_source([&]<typename Containers>(auto&& source) {
            typename Containers::template Map<u64, u64> map(source);
            const auto noise = insert_between_heap_noise(map, keys);
            for ([[maybe_unused]] auto iteration : state) {
                u64 sum = 0;
                for (const auto& [key, value] : map) {
                    sum += value;
                }
                benchmark::DoNotOptimize(sum);
            }
        });
        state.SetItemsProcessed(state.iterations() * static_cast<i64>(count));
        state.counters["elements"] = static_cast<f64>(count);
    }
};

struct UnorderedMapLookupPrebuilt {
    static constexpr std::string_view name = "unordered_map_lookup_prebuilt";
    static constexpr i64 maxElements = defaultMaxElements;
    static constexpr f64 hitRatio = 0.9;

    template <typename Variant>
    static void run(benchmark::State& state) {
        const auto count = static_cast<usize>(state.range(0));
        const std::span<const u64> keys = uniform_keys(count);
        const std::span<const u64> lookups = lookup_keys(count, count, hitRatio);
        Variant::with_source([&]<typename Containers>(auto&& source) {
            typename Containers::template UnorderedMap<u64, u64> map(source);
            const auto noise = insert_between_heap_noise(map, keys);
            for ([[maybe_unused]] auto iteration : state) {
                u64 sum = 0;
                for (const u64 key : lookups) {
                    if (const auto found = map.find(key); found != map.end()) {
                        sum += found->second;
                    }
                }
                benchmark::DoNotOptimize(sum);
            }
        });
        state.SetItemsProcessed(state.iterations() * static_cast<i64>(count));
        state.counters["elements"] = static_cast<f64>(count);
    }
};

} // namespace

void register_locality_benchmarks() {
    register_workload<MapLookupPrebuilt>(PrebuiltVariants{});
    register_workload<MapIteratePrebuilt>(PrebuiltVariants{});
    register_workload<UnorderedMapLookupPrebuilt>(PrebuiltVariants{});
}

} // namespace base::bench
