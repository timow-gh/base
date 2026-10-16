cmake_minimum_required(VERSION 3.28)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

include(detect_ninja)

# No C++20/23 modules are used; disable scanning to avoid clang-scan-deps
# lookup during compiler detection (project()) and the GCC + CMake 3.28
# incompatibility where -MD and -fdeps-format=p1689r5 are passed together.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

include(cmake/CPM.cmake)
if (NOT CPM_SOURCE_CACHE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/CPMSourceVariable.cmake")
    include(CPMSourceVariable.cmake)
endif ()

if (NOT CPM_SOURCE_CACHE)
    message(AUTHOR_WARNING "CPM_SOURCE_CACHE not set. The source files are downloaded for every build. This is not recommended.
        Set the CPM_SOURCE_CACHE variable to a directory of your choice to cache the cloned git repositories.
        Set the CPM_SOURCE_CACHE variable in CPMSourceVariable.cmake to keep your paths private.")
endif ()

project(base
        VERSION 0.1.0
        DESCRIPTION "Low-level foundation library: memory, platform and core utilities"
        HOMEPAGE_URL "https://github.com/timow-gh/base"
        LANGUAGES CXX)

include(base_components)

# Set common properties for cross platform builds when this is the root project.
if (PROJECT_IS_TOP_LEVEL)
    include(project_preamble)
    project_preamble()
endif ()

include(GNUInstallDirs)

if (PROJECT_IS_TOP_LEVEL)
    option(${PROJECT_NAME}_USE_SCCACHE "Use sccache" OFF)
    option(${PROJECT_NAME}_USE_WARNINGS "Compile using warnings." ON)
    option(${PROJECT_NAME}_WARNINGS_AS_ERRORS "Treat compiler warnings as errors " ON)
    option(${PROJECT_NAME}_STATIC_ANALYSIS "" ON)
    option(${PROJECT_NAME}_USE_CPPCHECK "" OFF)
    option(${PROJECT_NAME}_TESTS "" ON)
else ()
    option(${PROJECT_NAME}_USE_SCCACHE "Use sccache" OFF)
    option(${PROJECT_NAME}_USE_WARNINGS "Compile using warnings." OFF)
    option(${PROJECT_NAME}_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
    option(${PROJECT_NAME}_STATIC_ANALYSIS "" OFF)
    option(${PROJECT_NAME}_USE_CPPCHECK "" OFF)
    option(${PROJECT_NAME}_TESTS "" OFF)
endif ()

if (PROJECT_IS_TOP_LEVEL)
    set(${PROJECT_NAME}_INSTALL_DEFAULT ON)
else ()
    set(${PROJECT_NAME}_INSTALL_DEFAULT OFF)
endif ()
option(${PROJECT_NAME}_INSTALL "Install project targets" ${${PROJECT_NAME}_INSTALL_DEFAULT})
option(${PROJECT_NAME}_BUILD_DOCUMENTATION "Build API documentation with Doxygen" OFF)
# The components are static libraries. PIC lets consumers link them into their
# own shared libraries.
option(${PROJECT_NAME}_POSITION_INDEPENDENT_CODE "Compile the components as position independent code" ON)
option(${PROJECT_NAME}_PROFILING "Enable the BASE_PROFILE_* zones (defines BASE_ENABLE_PROFILING=1)" OFF)
set(${PROJECT_NAME}_PROFILING_REPORT_DIRECTORY "" CACHE STRING
        "Directory the profiling report is written to; empty prints it to stdout (BASE_PROFILE_REPORT_DIRECTORY)")
set(${PROJECT_NAME}_PROFILING_REPORT_NAME "profile" CACHE STRING
        "File name prefix of the profiling report (BASE_PROFILE_REPORT_NAME)")

if (${PROJECT_NAME}_USE_SCCACHE)
    include(setup_sccache)
    enable_sccache()
endif ()

if (${PROJECT_NAME}_USE_CPPCHECK)
    include(cppcheck)
    create_cppcheck_target()
endif ()

if (${PROJECT_NAME}_BUILD_DOCUMENTATION)
    include(setup_doxygen)
    enable_doxygen()
endif ()

# The component graph, in dependency order. A component may only depend on
# components declared before it.
base_declare_component(core)
base_declare_component(profile DEPENDS core)
base_declare_component(platform DEPENDS core)
base_declare_component(memory DEPENDS core platform)

# Consumers select what they need, e.g. -Dbase_COMPONENTS=memory or
# CPMAddPackage(... OPTIONS "base_COMPONENTS memory"); dependencies are added
# automatically.
set(${PROJECT_NAME}_COMPONENTS "${${PROJECT_NAME}_DECLARED_COMPONENTS}" CACHE STRING
        "Components of ${PROJECT_NAME} to build (dependencies are added automatically)")
base_resolve_components(${PROJECT_NAME}_RESOLVED_COMPONENTS ${${PROJECT_NAME}_COMPONENTS})
message(STATUS "${PROJECT_NAME} components: ${${PROJECT_NAME}_RESOLVED_COMPONENTS}")

add_subdirectory(dependencies)

# Before the components: they register their unit tests.
if (${PROJECT_NAME}_TESTS)
    include(CTest)
    enable_testing()
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

if (${PROJECT_NAME}_INSTALL)
    include(CMakePackageConfigHelpers)
    set(config_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")

    install(FILES "${PROJECT_SOURCE_DIR}/License"
            DESTINATION "${CMAKE_INSTALL_DOCDIR}"
            COMPONENT dev)

    write_basic_package_version_file(
            "${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion)

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
