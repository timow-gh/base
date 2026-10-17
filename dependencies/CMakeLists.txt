if (${PROJECT_NAME}_TESTS)
    include(FetchContent)
    add_subdirectory(googletest)
endif ()

if (${PROJECT_NAME}_BENCHMARKS)
    add_subdirectory(benchmark)
endif ()
