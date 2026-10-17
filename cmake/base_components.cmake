include_guard()

include(GNUInstallDirs)
include(warnings)
include(static_analysis)

# Components are declared once, in dependency order, in the root CMakeLists.txt.
# The declaration is the single source of truth for the dependency graph: it is
# used to resolve ${PROJECT_NAME}_COMPONENTS, to link the component targets and
# to write the dependency files of the installed package.
function(base_declare_component name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "DEPENDS")
    foreach (dependency IN LISTS arg_DEPENDS)
        if (NOT dependency IN_LIST ${PROJECT_NAME}_DECLARED_COMPONENTS)
            message(FATAL_ERROR "Component '${name}' depends on '${dependency}', which is not declared before it")
        endif ()
    endforeach ()
    set(${PROJECT_NAME}_COMPONENT_DEPENDS_${name} "${arg_DEPENDS}" PARENT_SCOPE)
    set(${PROJECT_NAME}_DECLARED_COMPONENTS ${${PROJECT_NAME}_DECLARED_COMPONENTS} ${name} PARENT_SCOPE)
endfunction()

# Expands the requested components by their transitive dependencies. The result
# keeps the declaration order, so dependencies always come before their users.
function(base_resolve_components out_var)
    set(requested ${ARGN})
    set(pending ${requested})
    set(closure "")
    while (pending)
        list(POP_FRONT pending name)
        if (NOT name IN_LIST ${PROJECT_NAME}_DECLARED_COMPONENTS)
            message(FATAL_ERROR "Unknown ${PROJECT_NAME} component '${name}'. "
                    "Available components: ${${PROJECT_NAME}_DECLARED_COMPONENTS}")
        endif ()
        if (NOT name IN_LIST closure)
            list(APPEND closure ${name})
            list(APPEND pending ${${PROJECT_NAME}_COMPONENT_DEPENDS_${name}})
        endif ()
    endwhile ()

    set(ordered "")
    foreach (name IN LISTS ${PROJECT_NAME}_DECLARED_COMPONENTS)
        if (name IN_LIST closure)
            list(APPEND ordered ${name})
        endif ()
    endforeach ()
    set(${out_var} ${ordered} PARENT_SCOPE)
endfunction()

# Creates the object library ${target}_header_check with one translation unit per
# header that only includes it. It gives the warnings and static analysis of a
# header-only component something to compile without putting them on the interface
# target, where they would reach consumers, and fails if a header does not compile
# on its own.
function(base_add_header_check target)
    set(check_target ${target}_header_check)
    set(check_dir "${CMAKE_CURRENT_BINARY_DIR}/header_check")
    set(check_sources "")
    foreach (header IN LISTS ARGN)
        string(REGEX REPLACE "^include/" "" include_path "${header}")
        string(MAKE_C_IDENTIFIER "${include_path}" source_name)
        set(source "${check_dir}/${source_name}.cpp")
        file(CONFIGURE OUTPUT "${source}"
                CONTENT "#include \"@include_path@\"\n"
                @ONLY)
        list(APPEND check_sources "${source}")
    endforeach ()

    add_library(${check_target} OBJECT ${check_sources})
    target_link_libraries(${check_target} PRIVATE ${target})
    set_target_properties(${check_target} PROPERTIES FOLDER ${PROJECT_NAME})
endfunction()

# Creates the library ${PROJECT_NAME}_<name> with the alias ${PROJECT_NAME}::<name>,
# links its declared dependencies and installs it as its own export set, so
# find_package() can load each component separately. Components with SOURCES are
# static libraries; header-only components are interface libraries, because a static
# library without sources has no link language.
function(base_add_component)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME" "SOURCES;HEADERS")
    if (NOT arg_NAME)
        message(FATAL_ERROR "base_add_component: NAME is required")
    endif ()

    set(name ${arg_NAME})
    set(target ${PROJECT_NAME}_${name})

    if (arg_SOURCES)
        add_library(${target} STATIC)
        set(scope PUBLIC)
        target_sources(${target} PRIVATE ${arg_SOURCES})
        set_target_properties(${target}
                PROPERTIES
                OUTPUT_NAME ${target}
                DEBUG_POSTFIX d
                # Static libraries that may end up inside a consumer's shared library.
                POSITION_INDEPENDENT_CODE ${${PROJECT_NAME}_POSITION_INDEPENDENT_CODE})
    else ()
        add_library(${target} INTERFACE)
        set(scope INTERFACE)
    endif ()
    add_library(${PROJECT_NAME}::${name} ALIAS ${target})

    target_sources(${target}
            ${scope}
            FILE_SET HEADERS
            BASE_DIRS include
            FILES ${arg_HEADERS})

    set_target_properties(${target}
            PROPERTIES
            EXPORT_NAME ${name}
            FOLDER ${PROJECT_NAME})

    target_compile_features(${target} ${scope} cxx_std_23)

    foreach (dependency IN LISTS ${PROJECT_NAME}_COMPONENT_DEPENDS_${name})
        target_link_libraries(${target} ${scope} ${PROJECT_NAME}::${dependency})
    endforeach ()

    if (arg_SOURCES)
        set(checked_target ${target})
    elseif (${PROJECT_NAME}_USE_WARNINGS OR ${PROJECT_NAME}_STATIC_ANALYSIS)
        base_add_header_check(${target} ${arg_HEADERS})
        set(checked_target ${target}_header_check)
    endif ()

    if (${PROJECT_NAME}_USE_WARNINGS)
        add_warnings_and_compile_options(${checked_target} "${${PROJECT_NAME}_WARNINGS_AS_ERRORS}")
    endif ()

    if (${PROJECT_NAME}_STATIC_ANALYSIS)
        enable_static_analysis(${checked_target} "${${PROJECT_NAME}_WARNINGS_AS_ERRORS}")
    endif ()

    set_property(GLOBAL APPEND PROPERTY ${PROJECT_NAME}_ENABLED_COMPONENTS ${name})

    if (${PROJECT_NAME}_INSTALL)
        set(config_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")

        install(TARGETS ${target}
                EXPORT ${target}Targets
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT dev
                FILE_SET HEADERS
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
                COMPONENT dev)

        install(EXPORT ${target}Targets
                NAMESPACE ${PROJECT_NAME}::
                FILE ${target}Targets.cmake
                DESTINATION "${config_install_dir}"
                COMPONENT dev)

        # Read by ${PROJECT_NAME}Config.cmake to load the dependencies of a requested
        # component before the component itself.
        set(dependencies_file "${CMAKE_CURRENT_BINARY_DIR}/${target}Dependencies.cmake")
        set(depends "${${PROJECT_NAME}_COMPONENT_DEPENDS_${name}}")
        file(CONFIGURE OUTPUT "${dependencies_file}"
                CONTENT "set(${target}_DEPENDS \"@depends@\")\n"
                @ONLY)
        install(FILES "${dependencies_file}"
                DESTINATION "${config_install_dir}"
                COMPONENT dev)
    endif ()
endfunction()

# Adds the GoogleTest executable ${PROJECT_NAME}_<name>_tests for a component and
# registers it with CTest as ${PROJECT_NAME}.<name>.
function(base_add_component_test)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME" "SOURCES")
    set(test_target ${PROJECT_NAME}_${arg_NAME}_tests)

    add_executable(${test_target} ${arg_SOURCES})
    target_link_libraries(${test_target} PRIVATE gtest_main ${PROJECT_NAME}::${arg_NAME})
    set_target_properties(${test_target} PROPERTIES FOLDER ${PROJECT_NAME}/tests)

    add_test(NAME ${PROJECT_NAME}.${arg_NAME} COMMAND ${test_target})
    set_tests_properties(${PROJECT_NAME}.${arg_NAME} PROPERTIES TIMEOUT 120)

    # Register for coverage dependencies
    set_property(GLOBAL APPEND PROPERTY ${PROJECT_NAME}_COVERAGE_TESTS ${test_target})
endfunction()
