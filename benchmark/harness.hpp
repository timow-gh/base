#pragma once

#include "variants.hpp"

#include "base/core/types.hpp"

#include <benchmark/benchmark.h>

#include <format>
#include <string_view>

namespace base::bench {

struct WorkloadSize {
    // Elements in the container; memory is reported per element.
    usize elements = 0;
    // Operations per iteration, reported as items per second.
    usize operations = 0;
};

/**
 * \brief Measures `work` with `Variant`: an untimed footprint pass, then the timed loop.
 *
 * `work` is a generic lambda that takes the container types as its template argument
 * and the allocation source (std::allocator, an Arena or a memory resource) as its
 * first argument, so one workload serves every variant:
 * \code
 * [keys]<typename Containers>(auto&& source, [[maybe_unused]] auto&& checkpoint) {
 *     typename Containers::template Vector<u64> values(source);
 *     ...
 * }
 * \endcode
 * Every container must be destroyed when `work` returns. checkpoint() records the
 * bytes held at that point in the footprint pass, reported as the counters
 * checkpoint_0, checkpoint_1, ...; in the timed loop it does nothing.
 *
 * The footprint pass runs with counting containers for std::allocator, which keeps
 * the bookkeeping out of the timed loop.
 */
template <typename Variant, typename Work>
void run_workload(benchmark::State& state, WorkloadSize size, Work work) {
    const Footprint footprint = Variant::measure(work);
    Variant::time(state, work, footprint);

    state.SetItemsProcessed(state.iterations() * static_cast<i64>(size.operations));
    state.counters["elements"] = static_cast<f64>(size.elements);
    state.counters["peak_bytes"] = static_cast<f64>(footprint.peakBytes);
    state.counters["allocated_bytes"] = static_cast<f64>(footprint.allocatedBytes);
    state.counters["bytes_per_element"] =
        static_cast<f64>(footprint.peakBytes) / static_cast<f64>(size.elements == 0U ? 1U : size.elements);
    if (footprint.allocations > 0U) {
        state.counters["allocations"] = static_cast<f64>(footprint.allocations);
    }
    for (usize i = 0; i < footprint.checkpoints.size(); ++i) {
        state.counters[std::format("checkpoint_{}", i)] = static_cast<f64>(footprint.checkpoints[i]);
    }
}

template <typename... Variants>
struct VariantList {};

using AllVariants = VariantList<StdVariant, PmrVariant, ArenaWarmVariant, ArenaColdVariant>;
// Without the cold arena: workloads on a container built before the timed loop
// would measure the same for both arena variants.
using PrebuiltVariants = VariantList<StdVariant, PmrVariant, ArenaWarmVariant>;

inline constexpr i64 minElements = 64;
inline constexpr i64 elementMultiplier = 8;
inline constexpr i64 defaultMaxElements = i64{1} << 20;

/**
 * \brief Registers `Workload::run<Variant>` for each variant as
 * `<Workload::name>/<Variant::name>/<elements>`, the names the report parses, for
 * 64 to Workload::maxElements elements in steps of 8×.
 */
template <typename Workload, typename... Variants>
void register_workload(VariantList<Variants...> /*variants*/) {
    (benchmark::RegisterBenchmark(std::format("{}/{}", Workload::name, Variants::name),
                                  &Workload::template run<Variants>)
         ->RangeMultiplier(elementMultiplier)
         ->Range(minElements, Workload::maxElements)
         ->Unit(benchmark::kMicrosecond)
         ->UseRealTime(),
     ...);
}

} // namespace base::bench
