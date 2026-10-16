base_add_component(
        NAME memory
        SOURCES
        source/arena.cpp
        HEADERS
        include/base/memory/arena.hpp)

if (${PROJECT_NAME}_TESTS)
    base_add_component_test(
            NAME memory
            SOURCES
            test/arena_test.cpp
            test/arena_debug_test.cpp)
endif ()
