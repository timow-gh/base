# Compares the arena containers with std::allocator and std::pmr containers on
# synthetic data. Not a component: nothing here is installed. Run it with
# benchmark/run.py, see benchmark/README.md.
set(benchmark_target ${PROJECT_NAME}_container_benchmarks)

add_executable(${benchmark_target}
        churn_benchmarks.cpp
        cli.cpp
        locality_benchmarks.cpp
        main.cpp
        map_benchmarks.cpp
        string_benchmarks.cpp
        synthetic_data.cpp
        unordered_benchmarks.cpp
        vector_benchmarks.cpp)
target_link_libraries(${benchmark_target} PRIVATE benchmark::benchmark ${PROJECT_NAME}::containers)
set_target_properties(${benchmark_target} PROPERTIES FOLDER ${PROJECT_NAME}/benchmarks)

# No static analysis, as for the tests: the registration macros of Google Benchmark
# trip clang-tidy, and the benchmarks are not shipped.
if (${PROJECT_NAME}_USE_WARNINGS)
    add_warnings_and_compile_options(${benchmark_target} "${${PROJECT_NAME}_WARNINGS_AS_ERRORS}")
endif ()

# The commit is not compiled in: benchmark/run.py passes it with --benchmark_context at
# run time, so it is right after every commit and marks uncommitted changes.
target_compile_definitions(${benchmark_target} PRIVATE "BASE_BENCH_BUILD_TYPE=\"$<CONFIG>\"")
