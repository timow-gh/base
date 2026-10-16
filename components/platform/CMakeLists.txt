if (WIN32)
    set(platform_backend_sources source/win32/virtual_memory.cpp)
else ()
    set(platform_backend_sources source/posix/virtual_memory.cpp)
endif ()

base_add_component(
        NAME platform
        SOURCES
        ${platform_backend_sources}
        HEADERS
        include/base/platform/virtual_memory.hpp)

if (WIN32)
    # Only the backend sources include <windows.h>; keep its macros out of them.
    target_compile_definitions(${PROJECT_NAME}_platform PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif ()

if (${PROJECT_NAME}_TESTS)
    base_add_component_test(
            NAME platform
            SOURCES
            test/virtual_memory_test.cpp)
endif ()
