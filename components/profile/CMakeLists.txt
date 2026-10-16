base_add_component(
        NAME profile
        SOURCES
        source/profile.cpp
        HEADERS
        include/base/profile/cpu_timer.hpp
        include/base/profile/profile.hpp)

# The definitions are PUBLIC, so base and every target that links it see the same
# values: the zones in both are switched on together, and the report output that
# profile.hpp documents is the one the library writes to.
if (${PROJECT_NAME}_PROFILING)
    target_compile_definitions(${PROJECT_NAME}_profile PUBLIC BASE_ENABLE_PROFILING=1)
endif ()

if (NOT "${${PROJECT_NAME}_PROFILING_REPORT_DIRECTORY}" STREQUAL "")
    # Forward slashes: backslashes would start escape sequences in the string literal.
    file(TO_CMAKE_PATH "${${PROJECT_NAME}_PROFILING_REPORT_DIRECTORY}" report_directory)
    target_compile_definitions(${PROJECT_NAME}_profile PUBLIC "BASE_PROFILE_REPORT_DIRECTORY=\"${report_directory}\"")
endif ()

if (NOT "${${PROJECT_NAME}_PROFILING_REPORT_NAME}" STREQUAL "profile")
    target_compile_definitions(${PROJECT_NAME}_profile
            PUBLIC "BASE_PROFILE_REPORT_NAME=\"${${PROJECT_NAME}_PROFILING_REPORT_NAME}\"")
endif ()

if (${PROJECT_NAME}_TESTS)
    base_add_component_test(
            NAME profile
            SOURCES
            test/cpu_timer_test.cpp
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
endif ()
