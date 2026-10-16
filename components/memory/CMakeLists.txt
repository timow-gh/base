base_add_component(
        NAME memory
        SOURCES
        source/arena.cpp
        source/scratch.cpp
        HEADERS
        include/base/memory/arena.hpp
        include/base/memory/arena_allocator.hpp
        include/base/memory/arena_containers.hpp
        include/base/memory/scratch.hpp)

if (${PROJECT_NAME}_TESTS)
    base_add_component_test(
            NAME memory
            SOURCES
            test/arena_allocator_test.cpp
            test/arena_test.cpp
            test/arena_debug_test.cpp
            test/scratch_test.cpp)
endif ()
