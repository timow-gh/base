# A separate project that uses the installed SDK, the way any other project
# would. It doubles as the example in ReadMe.md ("Using base from another
# project"). The package name is passed in by test/test_install, so this file
# does not repeat it.
cmake_minimum_required(VERSION 3.28)

project(consumer LANGUAGES CXX)

if (NOT PACKAGE_NAME)
    message(FATAL_ERROR "Pass the name of the installed package: -DPACKAGE_NAME=<name>")
endif ()

include(GNUInstallDirs)

# Request only containers: its dependencies must be loaded automatically.
find_package(${PACKAGE_NAME} CONFIG REQUIRED COMPONENTS containers)

foreach (dependency IN ITEMS core platform memory)
    if (NOT TARGET ${PACKAGE_NAME}::${dependency})
        message(FATAL_ERROR "find_package(${PACKAGE_NAME} COMPONENTS containers) did not load ${PACKAGE_NAME}::${dependency}")
    endif ()
endforeach ()

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE ${PACKAGE_NAME}::containers)
target_compile_features(consumer PRIVATE cxx_std_23)
set_target_properties(consumer PROPERTIES DEBUG_POSTFIX d)

install(TARGETS consumer
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
