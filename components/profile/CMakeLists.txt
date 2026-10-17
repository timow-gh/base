base_add_component(
        NAME profile
        SOURCES
        source/heap_tracking.cpp
        source/profile.cpp
        HEADERS
        include/base/profile/cpu_timer.hpp
        include/base/profile/heap_tracking.hpp
        include/base/profile/profile.hpp)

# The profiling mode (BASE_ENABLE_PROFILING, BASE_PROFILE_ALLOCATIONS) is defined on
# base::core, see components/core/CMakeLists.txt.

if (NOT "${${PROJECT_NAME}_PROFILING_REPORT_DIRECTORY}" STREQUAL "")
    # Forward slashes: backslashes would start escape sequences in the string literal.
    file(TO_CMAKE_PATH "${${PROJECT_NAME}_PROFILING_REPORT_DIRECTORY}" report_directory)
    target_compile_definitions(${PROJECT_NAME}_profile PUBLIC "BASE_PROFILE_REPORT_DIRECTORY=\"${report_directory}\"")
endif ()

if (NOT "${${PROJECT_NAME}_PROFILING_REPORT_NAME}" STREQUAL "profile")
    target_compile_definitions(${PROJECT_NAME}_profile
            PUBLIC "BASE_PROFILE_REPORT_NAME=\"${${PROJECT_NAME}_PROFILING_REPORT_NAME}\"")
endif ()

# Creates an object library that replaces the global operator new and operator delete
# with versions that report every heap allocation to record_heap_allocation(), so that
# heap_stats() and the allocation zones see them. `definitions` are added PRIVATE.
function(base_add_global_new_library target)
    add_library(${target} OBJECT source/global_new.cpp)
    target_link_libraries(${target} PUBLIC ${PROJECT_NAME}::profile)
    target_compile_definitions(${target} PRIVATE ${ARGN})
    set_target_properties(${target}
            PROPERTIES
            FOLDER ${PROJECT_NAME}
            POSITION_INDEPENDENT_CODE ${${PROJECT_NAME}_POSITION_INDEPENDENT_CODE})
    if (${PROJECT_NAME}_USE_WARNINGS)
        add_warnings_and_compile_options(${target} "${${PROJECT_NAME}_WARNINGS_AS_ERRORS}")
    endif ()
    if (${PROJECT_NAME}_STATIC_ANALYSIS)
        enable_static_analysis(${target} "${${PROJECT_NAME}_WARNINGS_AS_ERRORS}")
    endif ()
endfunction()

# base::profile_global_new tracks heap allocations in builds with
# base_PROFILING=ALLOCATIONS and compiles empty in every other build, so a program can
# link it unconditionally. Link it directly to the executable (or shared library), and
# to nothing that has its own replacement. A program with its own operator new leaves
# it out and reports from its own instead, see heap_tracking.hpp.
#
# - An object library, not a static one: the linker takes a static library member only
#   for symbols that are still undefined, and depending on the link order the C++
#   runtime may already have defined operator new.
# - On Windows a replacement applies to the module (exe or DLL) it is linked into only.
# - Under AddressSanitizer it takes over from ASan's operator new, so ASan no longer
#   reports new/delete mismatches; it still checks every access to the blocks.
set(global_new_target ${PROJECT_NAME}_profile_global_new)
base_add_global_new_library(${global_new_target})
add_library(${PROJECT_NAME}::profile_global_new ALIAS ${global_new_target})
set_target_properties(${global_new_target} PROPERTIES EXPORT_NAME profile_global_new)

if (${PROJECT_NAME}_INSTALL)
    # Part of the profile export set, so find_package(base COMPONENTS profile) defines it.
    install(TARGETS ${global_new_target}
            EXPORT ${PROJECT_NAME}_profileTargets
            OBJECTS DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT dev)
endif ()

if (${PROJECT_NAME}_TESTS)
    base_add_component_test(
            NAME profile
            SOURCES
            test/cpu_timer_test.cpp
            test/heap_tracking_test.cpp
            test/profile_allocations_test.cpp
            test/profile_disabled_test.cpp
            test/profile_test.cpp)

    # The report output is compiled into the library, so the file output is tested
    # with a second build of it that writes reports to a fixed directory.
    set(file_output_library ${PROJECT_NAME}_profile_file_output)
    add_library(${file_output_library} STATIC source/profile.cpp)
    target_include_directories(${file_output_library} PUBLIC include)
    target_link_libraries(${file_output_library} PUBLIC ${PROJECT_NAME}::core)
    target_compile_features(${file_output_library} PUBLIC cxx_std_23)
    target_compile_definitions(${file_output_library}
            PUBLIC
            BASE_ENABLE_PROFILING=1
            "BASE_PROFILE_REPORT_DIRECTORY=\"${CMAKE_CURRENT_BINARY_DIR}/profile-reports\""
            "BASE_PROFILE_REPORT_NAME=\"run\"")
    set_target_properties(${file_output_library} PROPERTIES FOLDER ${PROJECT_NAME}/tests)
    if (${PROJECT_NAME}_USE_WARNINGS)
        add_warnings_and_compile_options(${file_output_library} "${${PROJECT_NAME}_WARNINGS_AS_ERRORS}")
    endif ()

    set(file_test_target ${PROJECT_NAME}_profile_file_tests)
    add_executable(${file_test_target} test/profile_report_file_test.cpp)
    target_link_libraries(${file_test_target} PRIVATE gtest_main ${file_output_library})
    set_target_properties(${file_test_target} PROPERTIES FOLDER ${PROJECT_NAME}/tests)
    add_test(NAME ${PROJECT_NAME}.profile_file COMMAND ${file_test_target})
    set_tests_properties(${PROJECT_NAME}.profile_file PROPERTIES TIMEOUT 120)
    set_property(GLOBAL APPEND PROPERTY ${PROJECT_NAME}_COVERAGE_TESTS ${file_test_target})

    # Programs of their own: the replacement changes operator new for everything linked
    # into them, which the other profile tests must not depend on.
    #
    # The tracking replacement is a second build of global_new.cpp in allocation mode,
    # so that it is tested in every build, like the file output above.
    set(global_new_tracking_target ${PROJECT_NAME}_profile_global_new_tracking)
    base_add_global_new_library(${global_new_tracking_target} BASE_PROFILE_ALLOCATIONS=1)
    set_target_properties(${global_new_tracking_target} PROPERTIES FOLDER ${PROJECT_NAME}/tests)

    set(global_new_test_target ${PROJECT_NAME}_profile_global_new_tests)
    add_executable(${global_new_test_target} test/global_new_test.cpp)
    target_link_libraries(${global_new_test_target} PRIVATE gtest_main ${global_new_tracking_target})
    set_target_properties(${global_new_test_target} PROPERTIES FOLDER ${PROJECT_NAME}/tests)
    add_test(NAME ${PROJECT_NAME}.profile_global_new COMMAND ${global_new_test_target})
    set_tests_properties(${PROJECT_NAME}.profile_global_new PROPERTIES TIMEOUT 120)
    set_property(GLOBAL APPEND PROPERTY ${PROJECT_NAME}_COVERAGE_TESTS ${global_new_test_target})

    # Links the public target as a consumer does, to check that it replaces operator new
    # in allocation builds only.
    set(global_new_mode_test_target ${PROJECT_NAME}_profile_global_new_mode_tests)
    add_executable(${global_new_mode_test_target} test/global_new_mode_test.cpp)
    target_link_libraries(${global_new_mode_test_target} PRIVATE gtest_main ${PROJECT_NAME}::profile_global_new)
    set_target_properties(${global_new_mode_test_target} PROPERTIES FOLDER ${PROJECT_NAME}/tests)
    add_test(NAME ${PROJECT_NAME}.profile_global_new_mode COMMAND ${global_new_mode_test_target})
    set_tests_properties(${PROJECT_NAME}.profile_global_new_mode PROPERTIES TIMEOUT 120)
    set_property(GLOBAL APPEND PROPERTY ${PROJECT_NAME}_COVERAGE_TESTS ${global_new_mode_test_target})
endif ()
