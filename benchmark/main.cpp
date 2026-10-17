#include "workloads.hpp"

#include "base/core/config.hpp"
#include "base/memory/arena.hpp"

#include <benchmark/benchmark.h>

#include <string>

namespace {

[[nodiscard]] std::string compiler_name() {
#if BASE_COMPILER_CLANG
    return "clang " __clang_version__;
#elif BASE_COMPILER_MSVC
    return "msvc " + std::to_string(_MSC_FULL_VER);
#else
    return "gcc " __VERSION__;
#endif
}

// Recorded in the JSON "context", so the report can warn about results that do not
// measure an optimized build.
void add_context() {
    benchmark::AddCustomContext("base_build_type", BASE_BENCH_BUILD_TYPE);
    benchmark::AddCustomContext("base_commit", BASE_BENCH_GIT_COMMIT);
    benchmark::AddCustomContext("compiler", compiler_name());
    benchmark::AddCustomContext("arena_debug_checks", base::memory::Arena::debug_checks_enabled() ? "on" : "off");
    benchmark::AddCustomContext("asserts", BASE_ENABLE_ASSERTS ? "on" : "off");
    benchmark::AddCustomContext("asan", BASE_HAS_ASAN ? "on" : "off");
}

} // namespace

int main(int argc, char** argv) {
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    add_context();

    base::bench::register_vector_benchmarks();
    base::bench::register_string_benchmarks();
    base::bench::register_map_benchmarks();
    base::bench::register_unordered_benchmarks();
    base::bench::register_locality_benchmarks();
    base::bench::register_churn_benchmarks();

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
