# Uses only the core component of base from source, the way a project would with
# add_subdirectory() or CPMAddPackage(... OPTIONS "base_COMPONENTS core").
cmake_minimum_required(VERSION 3.28)

project(subdirectory_consumer LANGUAGES CXX)

if (NOT BASE_SOURCE_DIR)
    message(FATAL_ERROR "Pass the base source directory: -DBASE_SOURCE_DIR=<path>")
endif ()

set(base_COMPONENTS core CACHE STRING "" FORCE)
add_subdirectory("${BASE_SOURCE_DIR}" base)

if (NOT TARGET base::core)
    message(FATAL_ERROR "base::core was requested but not created")
endif ()
foreach (unwanted IN ITEMS platform memory)
    if (TARGET base::${unwanted})
        message(FATAL_ERROR "base::${unwanted} was not requested but was created")
    endif ()
endforeach ()

add_executable(subdirectory_consumer main.cpp)
target_link_libraries(subdirectory_consumer PRIVATE base::core)
