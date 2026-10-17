#pragma once

#include "implementations.hpp"

#include "base/core/types.hpp"

#include <benchmark/benchmark.h>

#include <format>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace base::bench {

struct WorkloadSize {
    // Elements in the container; memory is reported per element.
    usize elements = 0;
    // Operations per iteration, reported as items per second.
    usize operations = 0;
};

/**
 * \brief Measures `work` with `Implementation`: an untimed footprint pass, then the
 * timed loop.
 *
 * `work` is a generic lambda that takes the container types as its template argument
 * and the allocation source (std::allocator, an Arena or a memory resource) as its
 * first argument, so one workload serves every implementation:
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
template <typename Implementation, typename Work>
void run_workload(benchmark::State& state, WorkloadSize size, Work work) {
    // Google Benchmark calls a benchmark once per repetition and once per probe for the
    // iteration count, and the footprint is the same every time: measure it once.
    // State::name() leaves out the arguments, so the key adds the size.
    static std::map<std::string, Footprint, std::less<>> footprints;
    const std::string key = std::format("{}/{}", state.name(), state.range(0));
    auto found = footprints.find(key);
    if (found == footprints.end()) {
        found = footprints.emplace(key, Implementation::measure(work)).first;
    }
    const Footprint& footprint = found->second;
    Implementation::time(state, work, footprint);

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

} // namespace base::bench
