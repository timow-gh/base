# Uses only the core component of base from source, the way a project would with
# add_subdirectory() or CPMAddPackage(... OPTIONS "base_COMPONENTS core").
cmake_minimum_required(VERSION 3.28)

project(subdirectory_consumer LANGUAGES CXX)

if (NOT BASE_SOURCE_DIR)
    message(FATAL_ERROR "Pass the base source directory: -DBASE_SOURCE_DIR=<path>")
endif ()

set(base_COMPONENTS core CACHE STRING "" FORCE)
add_subdirectory("${BASE_SOURCE_DIR}" base)

if (NOT TARGET base::core)
    message(FATAL_ERROR "base::core was requested but not created")
endif ()
# A global property, because variables that base sets do not reach this scope. Read
# instead of a list of unwanted components, which would miss components added later.
get_property(built_components GLOBAL PROPERTY base_ENABLED_COMPONENTS)
if (NOT built_components STREQUAL "core")
    message(FATAL_ERROR "Only base::core was requested, but base built: ${built_components}")
endif ()
# base needs CPM only for its own tests and benchmarks, which are off in a subproject.
if (COMMAND CPMAddPackage)
    message(FATAL_ERROR "base loaded CPM although its tests and benchmarks are off")
endif ()

add_executable(subdirectory_consumer main.cpp)
target_link_libraries(subdirectory_consumer PRIVATE base::core)
