include_guard()

include(GNUInstallDirs)
include(warnings)
include(static_analysis)

# Declares a component and the components it depends on. Call it once per component,
# in the root CMakeLists.txt, before base_resolve_components(). The declarations are
# the only place the dependency graph is written down: base_resolve_components() reads
# them to find the components to build, base_add_component() to link each component
# to its dependencies and to write the dependency file of the installed package.
#
# Arguments:
#   name                 the component, e.g. memory (positional, required); its
#                        sources live in components/<name>/
#   DEPENDS <names>...   components it uses directly (optional); each one must be
#                        declared before it, which rules out dependency cycles
#
# Sets in the caller's scope:
#   <project>_DECLARED_COMPONENTS        all declared components, in declaration order
#   <project>_COMPONENT_DEPENDS_<name>   the DEPENDS list of this component
#
# Example:
#   base_declare_component(core)
#   base_declare_component(memory DEPENDS core platform)
function(base_declare_component name)
    # cmake_parse_arguments(PARSE_ARGV <skip> <prefix> <flags> <single-value keywords> <multi-value keywords>)
    # sorts the arguments of this call into variables named <prefix>_<KEYWORD>:
    #   PARSE_ARGV 1  skip the first argument: it is the positional `name` above
    #   arg           prefix of the result variables
    #   ""            flags, keywords without a value that become TRUE or FALSE: none
    #   ""            single-value keywords: none
    #   "DEPENDS"     multi-value keywords: DEPENDS core platform -> arg_DEPENDS = core;platform
    # Arguments that follow no keyword end up in arg_UNPARSED_ARGUMENTS.
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "DEPENDS")
    if (arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "base_declare_component(${name}): unexpected arguments '${arg_UNPARSED_ARGUMENTS}'")
    endif ()

    foreach (dependency IN LISTS arg_DEPENDS)
        if (NOT dependency IN_LIST ${PROJECT_NAME}_DECLARED_COMPONENTS)
            message(FATAL_ERROR "Component '${name}' depends on '${dependency}', which is not declared before it")
        endif ()
    endforeach ()
    # A function has its own variable scope; PARENT_SCOPE sets the variables in the caller.
    set(${PROJECT_NAME}_COMPONENT_DEPENDS_${name} "${arg_DEPENDS}" PARENT_SCOPE)
    set(${PROJECT_NAME}_DECLARED_COMPONENTS ${${PROJECT_NAME}_DECLARED_COMPONENTS} ${name} PARENT_SCOPE)
endfunction()

# Resolves the requested components: expands them by everything they depend on,
# directly or through other components, so that requesting memory also builds core
# and platform. Call it after all base_declare_component() calls.
#
# Arguments:
#   out_var        variable to store the result in (positional, required)
#   components...  the requested components (any further arguments); an unknown
#                  name stops the configure with the list of declared components
#
# The result keeps the declaration order, so every component comes after its
# dependencies and the components can be added in this order. O(C^2) for C declared
# components, which are few.
#
# Example:
#   base_resolve_components(resolved memory)   # resolved = core;platform;memory
function(base_resolve_components out_var)
    # ARGN holds the arguments after the named parameter `out_var`.
    set(pending ${ARGN})
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

# Creates the object library <target>_header_check, with one generated translation
# unit per header that only includes it. It gives the warnings and static analysis of
# a header-only component something to compile without putting them on the interface
# target, where they would reach consumers, and fails if a header does not compile on
# its own. Called by base_add_component() for header-only components.
#
# Arguments:
#   target      the interface library whose headers are checked (positional, required)
#   headers...  its headers, relative to the component directory (any further
#               arguments), e.g. include/base/containers/arena_vector.hpp
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

# Creates the library of a component declared with base_declare_component(). Call it
# once, in components/<name>/CMakeLists.txt.
#
# - With SOURCES it is a static library; without, a header-only interface library,
#   because a static library without sources has no link language.
# - Links the declared dependencies, PUBLIC (static) or INTERFACE (header-only), so
#   that whoever links the component gets them as well.
# - Applies the warnings and static analysis when their options are on. Header-only
#   components get them through base_add_header_check() instead.
# - With <project>_INSTALL on, installs the library and its headers, and installs it
#   as its own export set, so find_package(<project> COMPONENTS <name>) can load each
#   component separately.
#
# Arguments (keywords, in any order):
#   NAME <name>             the declared component (required)
#   SOURCES <files>...      .cpp files, relative to the component directory (optional)
#   HEADERS <files>...      public headers under include/, relative to the component
#                           directory; installed to <prefix>/include/ with the same
#                           relative path below include/
#
# Creates:
#   <project>_<name>        the library target, e.g. base_memory
#   <project>::<name>       an alias of it, the name to link against, e.g. base::memory
#
# Example:
#   base_add_component(
#           NAME memory
#           SOURCES source/arena.cpp
#           HEADERS include/base/memory/arena.hpp)
function(base_add_component)
    # cmake_parse_arguments(PARSE_ARGV <skip> <prefix> <flags> <single-value keywords> <multi-value keywords>)
    # sorts the arguments of this call into variables named <prefix>_<KEYWORD>:
    #   PARSE_ARGV 0       start at the first argument: there are no positional ones
    #   arg                prefix of the result variables
    #   ""                 flags, keywords without a value that become TRUE or FALSE: none
    #   "NAME"             single-value keywords: NAME memory -> arg_NAME = memory
    #   "SOURCES;HEADERS"  multi-value keywords: SOURCES a.cpp b.cpp -> arg_SOURCES = a.cpp;b.cpp
    # Arguments that follow no keyword end up in arg_UNPARSED_ARGUMENTS. A missing
    # keyword leaves its variable undefined.
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME" "SOURCES;HEADERS")
    if (NOT arg_NAME)
        message(FATAL_ERROR "base_add_component: NAME is required")
    endif ()
    if (arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "base_add_component(NAME ${arg_NAME}): unexpected arguments '${arg_UNPARSED_ARGUMENTS}'")
    endif ()

    set(name ${arg_NAME})
    set(target ${PROJECT_NAME}_${name})

    if (arg_SOURCES)
        add_library(${target} STATIC)
        set(scope PUBLIC)
        target_sources(${target} PRIVATE ${arg_SOURCES})
        set_target_properties(${target} PROPERTIES DEBUG_POSTFIX d)
    else ()
        add_library(${target} INTERFACE)
        set(scope INTERFACE)
    endif ()
    add_library(${PROJECT_NAME}::${name} ALIAS ${target})

    # A header file set: the headers are found below include/ when building, and
    # install(TARGETS ... FILE_SET HEADERS) installs them with the same layout.
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

    # Global, unlike a variable, so a project that adds base with add_subdirectory()
    # can read which components were built.
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

# Adds the GoogleTest executable <project>_<name>_tests for a component, linked to
# gtest_main and the component, and registers it with CTest as <project>.<name>.
# Call it in components/<name>/CMakeLists.txt, inside if (<project>_TESTS).
#
# Arguments (keywords, in any order):
#   NAME <name>          the component under test (required)
#   SOURCES <files>...   the test .cpp files, relative to the component directory
#
# Example:
#   base_add_component_test(
#           NAME memory
#           SOURCES test/arena_test.cpp test/scratch_test.cpp)
function(base_add_component_test)
    # Sorts the arguments into arg_NAME and arg_SOURCES; see base_add_component()
    # for how cmake_parse_arguments works.
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME" "SOURCES")
    if (NOT arg_NAME)
        message(FATAL_ERROR "base_add_component_test: NAME is required")
    endif ()
    if (arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "base_add_component_test(NAME ${arg_NAME}): unexpected arguments '${arg_UNPARSED_ARGUMENTS}'")
    endif ()

    set(test_target ${PROJECT_NAME}_${arg_NAME}_tests)

    add_executable(${test_target} ${arg_SOURCES})
    target_link_libraries(${test_target} PRIVATE gtest_main ${PROJECT_NAME}::${arg_NAME})
    set_target_properties(${test_target} PROPERTIES FOLDER ${PROJECT_NAME}/tests)

    add_test(NAME ${PROJECT_NAME}.${arg_NAME} COMMAND ${test_target})
    set_tests_properties(${PROJECT_NAME}.${arg_NAME} PROPERTIES TIMEOUT 120)

    # Read by cmake/coverage.cmake: the coverage-report target builds these first.
    set_property(GLOBAL APPEND PROPERTY ${PROJECT_NAME}_COVERAGE_TESTS ${test_target})
endfunction()
