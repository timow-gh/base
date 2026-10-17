# googletest is a test-only dependency: it is never installed (INSTALL_GTEST OFF)
# and never shipped, so it is built static even if the caller sets BUILD_SHARED_LIBS.
# gtest_force_shared_crt OFF is its default. Its rewrite of /MD to /MT has no effect
# under policy CMP0091 (the CRT comes from CMAKE_MSVC_RUNTIME_LIBRARY, not from the
# flags), so gtest uses the same CRT as base and the tests.
CPMAddPackage(
        NAME googletest
        GITHUB_REPOSITORY google/googletest
        GIT_TAG v1.17.0
        GIT_SHALLOW TRUE
        OPTIONS
            "INSTALL_GTEST OFF"
            "BUILD_SHARED_LIBS OFF"
            "gtest_force_shared_crt OFF"
)

if(googletest_ADDED)
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        target_compile_options(gtest_main INTERFACE
                "-Wno-weak-vtables"
                )
    endif ()
endif()