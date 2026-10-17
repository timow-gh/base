# Google Benchmark is a dev-only dependency of the benchmarks in benchmark/: it is
# never installed and never shipped, so it is built static regardless of the
# project's BUILD_SHARED_LIBS, like googletest.
CPMAddPackage(
        NAME benchmark
        GITHUB_REPOSITORY google/benchmark
        GIT_TAG v1.9.1
        GIT_SHALLOW TRUE
        OPTIONS
            "BENCHMARK_ENABLE_TESTING OFF"
            "BENCHMARK_ENABLE_GTEST_TESTS OFF"
            "BENCHMARK_ENABLE_INSTALL OFF"
            "BENCHMARK_INSTALL_DOCS OFF"
            "BENCHMARK_ENABLE_WERROR OFF"
            "BUILD_SHARED_LIBS OFF"
)

if (benchmark_ADDED)
    # Third-party headers: our warnings (as errors) must not apply to them.
    set_target_properties(benchmark PROPERTIES SYSTEM TRUE)
endif ()
