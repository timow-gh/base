# The component unit tests live next to their components (components/<name>/test).
# The tests here check how other projects consume base.

# Shared by the nested consumer builds: they must match this build's compiler,
# flags and CRT (e.g. sanitizer flags, -stdlib, /MD vs /MT).
set(consumer_toolchain_args
        -G "${CMAKE_GENERATOR}"
        "-DCMAKE_BUILD_TYPE=$<CONFIG>"
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
        "-DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}")
if (CMAKE_GENERATOR_PLATFORM)
    list(APPEND consumer_toolchain_args -A "${CMAKE_GENERATOR_PLATFORM}")
endif ()
if (CMAKE_GENERATOR_TOOLSET)
    list(APPEND consumer_toolchain_args -T "${CMAKE_GENERATOR_TOOLSET}")
endif ()
if (CMAKE_MAKE_PROGRAM)
    list(APPEND consumer_toolchain_args "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
endif ()
if (MSVC AND CMAKE_MSVC_RUNTIME_LIBRARY)
    list(APPEND consumer_toolchain_args "-DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}")
endif ()

# test_install consumes the installed SDK through find_package(COMPONENTS ...).
option(${PROJECT_NAME}_TEST_INSTALL "Test consuming the installed ${PROJECT_NAME} package" ON)
if (${PROJECT_NAME}_TEST_INSTALL AND ${PROJECT_NAME}_INSTALL AND "containers" IN_LIST ${PROJECT_NAME}_RESOLVED_COMPONENTS)
    add_subdirectory(test_install)
endif ()

# test_subdirectory consumes the source tree with add_subdirectory() and builds
# only the requested components.
option(${PROJECT_NAME}_TEST_SUBDIRECTORY "Test consuming ${PROJECT_NAME} with add_subdirectory()" ON)
if (${PROJECT_NAME}_TEST_SUBDIRECTORY)
    add_subdirectory(test_subdirectory)
endif ()
