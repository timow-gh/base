#pragma once

#include "registry.hpp"
#include "suites.hpp"

#include "base/core/types.hpp"

#include <expected>
#include <span>
#include <string>
#include <vector>

namespace base::bench {

/**
 * \brief The command line, split into our own flags and the arguments that go on to
 * Google Benchmark.
 */
struct Options {
    const Suite* suite = nullptr;
    // Empty selects everything; filters combine with AND.
    std::vector<std::string> containers;
    std::vector<std::string> workloads;
    std::vector<std::string> implementations;
    std::vector<std::string> tags;
    // Empty: the suite's sizes.
    std::vector<i64> sizes;
    bool list = false;
    bool describe = false;
    bool help = false;
    // argv[0] and every argument that is not ours, e.g. --benchmark_filter.
    std::vector<std::string> benchmarkArguments;
};

/**
 * \brief A spec that passed the filters, with the sizes it runs at: the selected sizes
 * up to the workload's maxElements.
 */
struct Selection {
    BenchmarkSpec spec;
    std::vector<i64> sizes;
};

/**
 * \brief Parses our flags and checks every name against `specs`, so a typo fails with
 * the valid names instead of silently selecting nothing.
 */
[[nodiscard]] std::expected<Options, std::string> parse_options(std::span<char* const> arguments,
                                                                std::span<const BenchmarkSpec> specs);

// The specs that pass all filters of `options`, with their sizes; specs left without a
// size are dropped.
[[nodiscard]] std::vector<Selection> select(std::span<const BenchmarkSpec> specs, const Options& options);

// "none", or the active filters, e.g. "container=map impl=std,arena".
[[nodiscard]] std::string describe_filters(const Options& options);

/**
 * \brief The arguments for benchmark::Initialize: the passed-through ones plus the
 * suite's repetitions, min time and interleaving, each only if not given explicitly.
 */
[[nodiscard]] std::vector<std::string> benchmark_arguments(const Options& options);

[[nodiscard]] std::string help_text();

// One line per container and workload, then the totals.
[[nodiscard]] std::string list_text(std::span<const Selection> selection, const Options& options);

// Registers each selection as <container>/<workload>/<implementation>/<size>.
void register_benchmarks(std::span<const Selection> selection);

// Records the build, the suite and the filters in the JSON context, where the report
// reads them. Kept short: Google Benchmark also prints the context on the console.
void add_context(std::span<const Selection> selection, const Options& options);

/**
 * \brief The description, operation and tags of every workload and the description of
 * every implementation, as JSON for the report (--describe).
 */
[[nodiscard]] std::string describe_json(std::span<const BenchmarkSpec> specs);

} // namespace base::bench
