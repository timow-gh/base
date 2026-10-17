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

# Recorded in the results so a report names the code it measured. Taken at configure
# time, so commits made after configuring are not reflected until the next configure.
find_package(Git QUIET)
set(git_commit "unknown")
if (GIT_FOUND)
    execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            OUTPUT_VARIABLE git_commit
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
endif ()
target_compile_definitions(${benchmark_target} PRIVATE
        "BASE_BENCH_GIT_COMMIT=\"${git_commit}\""
        "BASE_BENCH_BUILD_TYPE=\"$<CONFIG>\"")
