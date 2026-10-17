#include "cli.hpp"
#include "registry.hpp"
#include "workloads.hpp"

#include "base/core/types.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

using namespace base::types;

int main(int argc, char** argv) {
    using base::bench::BenchmarkSpec;

    std::vector<BenchmarkSpec> specs = base::bench::concat({
        base::bench::vector_benchmarks(),
        base::bench::string_benchmarks(),
        base::bench::map_benchmarks(),
        base::bench::unordered_benchmarks(),
        base::bench::locality_benchmarks(),
        base::bench::churn_benchmarks(),
    });
    // Grouped by container for --list and the run order; stable, so each file's order
    // of workloads stays.
    std::ranges::stable_sort(specs, {}, [](const BenchmarkSpec& spec) {
        return std::ranges::find(base::bench::containerOrder, spec.container) - base::bench::containerOrder.begin();
    });

    const auto options = base::bench::parse_options(std::span<char* const>(argv, static_cast<usize>(argc)), specs);
    if (!options) {
        std::fprintf(stderr, "error: %s\n", options.error().c_str());
        return 2;
    }

    if (options->describe) {
        std::fputs(base::bench::describe_json(specs).c_str(), stdout);
        return 0;
    }

    std::vector<std::string> arguments = base::bench::benchmark_arguments(*options);
    if (options->help) {
        std::fputs(base::bench::help_text().c_str(), stdout);
        // Google Benchmark prints its own flags for --help and exits.
        arguments.emplace_back("--help");
    }

    const std::vector<base::bench::Selection> selection = base::bench::select(specs, *options);
    if (options->list) {
        std::fputs(base::bench::list_text(selection, *options).c_str(), stdout);
        return 0;
    }
    if (selection.empty() && !options->help) {
        std::fputs("error: no benchmark matches the filters; --list shows what each filter selects\n", stderr);
        return 2;
    }

    std::vector<char*> argumentPointers;
    for (std::string& argument : arguments) {
        argumentPointers.push_back(argument.data());
    }
    argumentPointers.push_back(nullptr);
    int argumentCount = static_cast<int>(arguments.size());
    benchmark::Initialize(&argumentCount, argumentPointers.data());
    if (benchmark::ReportUnrecognizedArguments(argumentCount, argumentPointers.data())) {
        return 1;
    }

    base::bench::add_context(selection, *options);
    base::bench::register_benchmarks(selection);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
