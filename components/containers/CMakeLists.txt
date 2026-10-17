base_add_component(
        NAME containers
        HEADERS
        include/base/containers/arena_map.hpp
        include/base/containers/arena_set.hpp
        include/base/containers/arena_string.hpp
        include/base/containers/arena_unordered_map.hpp
        include/base/containers/arena_unordered_set.hpp
        include/base/containers/arena_vector.hpp)

if (${PROJECT_NAME}_TESTS)
    base_add_component_test(
            NAME containers
            SOURCES
            test/arena_containers_test.cpp)
endif ()
