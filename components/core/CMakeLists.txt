base_add_component(
        NAME core
        SOURCES
        source/assert.cpp
        HEADERS
        include/base/core/asan.hpp
        include/base/core/assert.hpp
        include/base/core/bits.hpp
        include/base/core/config.hpp
        include/base/core/units.hpp)

if (${PROJECT_NAME}_TESTS)
    base_add_component_test(
            NAME core
            SOURCES
            test/assert_test.cpp
            test/bits_test.cpp
            test/units_test.cpp)
endif ()
