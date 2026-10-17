base_add_component(
        NAME core
        SOURCES
        source/assert.cpp
        HEADERS
        include/base/core/alignment.hpp
        include/base/core/asan.hpp
        include/base/core/assert.hpp
        include/base/core/atomic.hpp
        include/base/core/config.hpp
        include/base/core/types.hpp
        include/base/core/units.hpp)

# On core, not on profile, and PUBLIC: every component and consumer must agree on the
# profiling mode, and both profile and code outside it (a program's own operator new)
# read it through config.hpp.
if (${PROJECT_NAME}_PROFILING STREQUAL "TIME")
    target_compile_definitions(${PROJECT_NAME}_core PUBLIC BASE_ENABLE_PROFILING=1)
elseif (${PROJECT_NAME}_PROFILING STREQUAL "ALLOCATIONS")
    target_compile_definitions(${PROJECT_NAME}_core PUBLIC BASE_ENABLE_PROFILING=1 BASE_PROFILE_ALLOCATIONS=1)
endif ()

if (${PROJECT_NAME}_TESTS)
    base_add_component_test(
            NAME core
            SOURCES
            test/alignment_test.cpp
            test/assert_test.cpp
            test/atomic_test.cpp
            test/types_test.cpp
            test/units_test.cpp)
endif ()
