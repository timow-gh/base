# Compares the arena containers with std::allocator and std::pmr containers on
# synthetic data. Not a component: nothing here is installed. See ReadMe.md,
# "Benchmarks".
set(benchmark_target ${PROJECT_NAME}_container_benchmarks)

add_executable(${benchmark_target}
        churn_benchmarks.cpp
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

# Repetitions in random order give the report the spread of every measurement and
# spread CPU frequency or thermal drift over all variants instead of biasing one.
# Override for a quick run, e.g. -Dbase_BENCHMARK_ARGS="--benchmark_filter=/64$;--benchmark_repetitions=2".
set(${PROJECT_NAME}_BENCHMARK_ARGS
        "--benchmark_repetitions=10;--benchmark_enable_random_interleaving=true;--benchmark_min_time=0.2s"
        CACHE STRING "Arguments of the benchmark run of the benchmark_report target")

find_package(Python3 COMPONENTS Interpreter)
if (Python3_FOUND)
    set(results_file "${CMAKE_CURRENT_BINARY_DIR}/results.json")
    set(report_file "${CMAKE_CURRENT_BINARY_DIR}/report.html")
    add_custom_target(benchmark_report
            COMMAND $<TARGET_FILE:${benchmark_target}>
            ${${PROJECT_NAME}_BENCHMARK_ARGS}
            --benchmark_report_aggregates_only=false
            "--benchmark_out=${results_file}"
            --benchmark_out_format=json
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/report/make_report.py"
            "${results_file}" "${report_file}"
            COMMAND ${CMAKE_COMMAND} -E echo "Benchmark report: ${report_file}"
            USES_TERMINAL
            VERBATIM)
    add_dependencies(benchmark_report ${benchmark_target})
    set_target_properties(benchmark_report PROPERTIES FOLDER ${PROJECT_NAME}/benchmarks)
else ()
    message(WARNING "Python 3 not found: the benchmark_report target is not available")
endif ()
