cmake_minimum_required(VERSION 3.28)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

# No C++20/23 modules are used; disable scanning to avoid clang-scan-deps
# lookup during compiler detection (project()) and the GCC + CMake 3.28
# incompatibility where -MD and -fdeps-format=p1689r5 are passed together.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

project(base
        VERSION 0.1.0
        DESCRIPTION "Low-level foundation library: memory, platform and core utilities"
        HOMEPAGE_URL "https://github.com/timow-gh/base"
        LANGUAGES CXX)

include(base_components)
include(GNUInstallDirs)

# Settings for the whole build, which a parent project decides for itself.
if (PROJECT_IS_TOP_LEVEL)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    # Keeps base's symbols out of the exports of a shared library that links it.
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
    # One place for all executables (multi-config generators add /<Config>);
    # benchmark/runner.py looks for the benchmarks in bin/.
    if (NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
    endif ()
endif ()

# The development options default to ON only when base is the top-level project, so
# a parent project gets neither our tests nor our warnings as errors.
option(${PROJECT_NAME}_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(${PROJECT_NAME}_INSTALL "Generate the install rules" ${PROJECT_IS_TOP_LEVEL})
option(${PROJECT_NAME}_USE_WARNINGS "Compile with warnings" ${PROJECT_IS_TOP_LEVEL})
option(${PROJECT_NAME}_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ${PROJECT_IS_TOP_LEVEL})
option(${PROJECT_NAME}_STATIC_ANALYSIS "Run clang-tidy (Clang) or MSVC code analysis" ${PROJECT_IS_TOP_LEVEL})
# Off even for the top-level project: the benchmarks download Google Benchmark and
# only make sense in release builds, see the *-benchmark presets.
option(${PROJECT_NAME}_BENCHMARKS "Build the container benchmarks (Google Benchmark)" OFF)

# The components are static libraries. On Linux they must be compiled as PIC to be
# linked into a consumer's shared library; MSVC ignores it and macOS defaults to it.
# Set in base's directory scope only, so a parent project's own setting is kept.
if (NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif ()

# TIME and ALLOCATIONS exclude each other: counting allocations slows every operator
# new down, which would distort the times measured next to it.
set(profiling_modes OFF TIME ALLOCATIONS)
set(${PROJECT_NAME}_PROFILING OFF CACHE STRING
        "Profiling mode: OFF, TIME (the BASE_PROFILE_* zones measure time) or ALLOCATIONS (they count heap allocations)")
set_property(CACHE ${PROJECT_NAME}_PROFILING PROPERTY STRINGS ${profiling_modes})
if (NOT ${PROJECT_NAME}_PROFILING IN_LIST profiling_modes)
    list(JOIN profiling_modes ", " profiling_mode_names)
    message(FATAL_ERROR "${PROJECT_NAME}_PROFILING is '${${PROJECT_NAME}_PROFILING}', expected one of: ${profiling_mode_names}")
endif ()
set(${PROJECT_NAME}_PROFILING_REPORT_DIRECTORY "" CACHE STRING
        "Directory the profiling report is written to; empty prints it to stdout (BASE_PROFILE_REPORT_DIRECTORY)")
set(${PROJECT_NAME}_PROFILING_REPORT_NAME "profile" CACHE STRING
        "File name prefix of the profiling report (BASE_PROFILE_REPORT_NAME)")

# The component graph, in dependency order. A component may only depend on
# components declared before it.
base_declare_component(core)
base_declare_component(platform DEPENDS core)
# profile needs platform for the heap that its replacement of operator new allocates from.
base_declare_component(profile DEPENDS core platform)
base_declare_component(memory DEPENDS core platform)
# memory must never depend on containers: allocators keep their own bookkeeping in
# memory::detail, so the containers can be built on top of them.
base_declare_component(containers DEPENDS core memory)

# Consumers select what they need, e.g. -Dbase_COMPONENTS=memory or
# CPMAddPackage(... OPTIONS "base_COMPONENTS memory"); dependencies are added
# automatically. The default is empty rather than the declared list: a list stored
# in the cache would leave components declared later out of existing build trees.
set(${PROJECT_NAME}_COMPONENTS "" CACHE STRING
        "Components of ${PROJECT_NAME} to build; empty builds all of them (dependencies are added automatically)")
set(requested_components ${${PROJECT_NAME}_COMPONENTS})
if (NOT requested_components)
    set(requested_components ${${PROJECT_NAME}_DECLARED_COMPONENTS})
endif ()
base_resolve_components(${PROJECT_NAME}_RESOLVED_COMPONENTS ${requested_components})
message(STATUS "${PROJECT_NAME} components: ${${PROJECT_NAME}_RESOLVED_COMPONENTS}")

if (${PROJECT_NAME}_BENCHMARKS AND NOT "containers" IN_LIST ${PROJECT_NAME}_RESOLVED_COMPONENTS)
    message(FATAL_ERROR "${PROJECT_NAME}_BENCHMARKS needs the containers component")
endif ()

add_subdirectory(dependencies)

# Before the components: they register their unit tests.
if (${PROJECT_NAME}_TESTS)
    include(CTest)
endif ()

foreach (component IN LISTS ${PROJECT_NAME}_RESOLVED_COMPONENTS)
    add_subdirectory(components/${component})
endforeach ()

# Umbrella target for consumers that want everything that was built.
add_library(${PROJECT_NAME}_all INTERFACE)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME}_all)
foreach (component IN LISTS ${PROJECT_NAME}_RESOLVED_COMPONENTS)
    target_link_libraries(${PROJECT_NAME}_all INTERFACE ${PROJECT_NAME}::${component})
endforeach ()

if (${PROJECT_NAME}_TESTS)
    add_subdirectory(test)

    # Include coverage report generation if coverage flags are enabled
    if(CMAKE_CXX_FLAGS MATCHES "--coverage")
        message(STATUS "Coverage enabled - adding coverage-report target")
        include(${PROJECT_SOURCE_DIR}/cmake/coverage.cmake)
    endif()
endif ()

if (${PROJECT_NAME}_BENCHMARKS)
    add_subdirectory(benchmark)
endif ()

if (${PROJECT_NAME}_INSTALL)
    include(CMakePackageConfigHelpers)
    set(config_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")

    install(FILES "${PROJECT_SOURCE_DIR}/License"
            DESTINATION "${CMAKE_INSTALL_DOCDIR}"
            COMPONENT dev)

    # SameMinorVersion until 1.0: before 1.0 a minor version may break the API.
    write_basic_package_version_file(
            "${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMinorVersion)

    # Referenced as @project_name@ and @installed_components@ inside Config.cmake.in
    set(project_name "${PROJECT_NAME}")
    set(installed_components "${${PROJECT_NAME}_RESOLVED_COMPONENTS}")
    configure_package_config_file(
            "${PROJECT_SOURCE_DIR}/cmake/Config.cmake.in"
            "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
            INSTALL_DESTINATION "${config_install_dir}")

    install(FILES
            "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
            "${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
            DESTINATION "${config_install_dir}"
            COMPONENT dev)
endif ()
