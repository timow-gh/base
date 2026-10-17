# CPM is needed only for the third-party dependencies of the tests and benchmarks;
# the components themselves have none, so consumers never load it.
if (NOT (${PROJECT_NAME}_TESTS OR ${PROJECT_NAME}_BENCHMARKS))
    return()
endif ()

include("${PROJECT_SOURCE_DIR}/cmake/CPM.cmake")
if (NOT CPM_SOURCE_CACHE AND EXISTS "${PROJECT_SOURCE_DIR}/CPMSourceVariable.cmake")
    include("${PROJECT_SOURCE_DIR}/CPMSourceVariable.cmake")
endif ()

if (NOT CPM_SOURCE_CACHE)
    message(AUTHOR_WARNING "CPM_SOURCE_CACHE not set. The source files are downloaded for every build. This is not recommended.
        Set the CPM_SOURCE_CACHE variable to a directory of your choice to cache the cloned git repositories.
        Set the CPM_SOURCE_CACHE variable in CPMSourceVariable.cmake to keep your paths private.")
endif ()

if (${PROJECT_NAME}_TESTS)
    add_subdirectory(googletest)
endif ()

if (${PROJECT_NAME}_BENCHMARKS)
    add_subdirectory(benchmark)
endif ()
