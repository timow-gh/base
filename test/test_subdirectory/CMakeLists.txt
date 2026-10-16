# Configures and builds consumer/, which adds the base source tree with
# add_subdirectory() and base_COMPONENTS=core. The consumer fails to configure if
# components that were not requested are built anyway.
set(consumer_dir "${CMAKE_CURRENT_BINARY_DIR}/consumer")

add_test(NAME Test${PROJECT_NAME}SubdirectoryConfigure
        COMMAND ${CMAKE_COMMAND}
        -S "${CMAKE_CURRENT_LIST_DIR}/consumer"
        -B "${consumer_dir}"
        ${consumer_toolchain_args}
        "-DBASE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DCPM_SOURCE_CACHE=${CPM_SOURCE_CACHE}")

add_test(NAME Test${PROJECT_NAME}SubdirectoryBuild
        COMMAND ${CMAKE_COMMAND} --build "${consumer_dir}" --config $<CONFIG>)

set_tests_properties(Test${PROJECT_NAME}SubdirectoryConfigure PROPERTIES
        TIMEOUT 300
        FIXTURES_SETUP ${PROJECT_NAME}_subdirectory)
set_tests_properties(Test${PROJECT_NAME}SubdirectoryBuild PROPERTIES
        TIMEOUT 300
        FIXTURES_REQUIRED ${PROJECT_NAME}_subdirectory)
