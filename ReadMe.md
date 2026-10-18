# base

A C++23 library of components, each a CMake target `base::<component>` (for
example `base::memory`). Linking a component links its dependencies.
`base::base` links every component that was built.

## Requirements

- CMake 3.28 or newer
- GCC 14, Clang 20 or MSVC 19.40 or newer

## Integration

**From source**, with [CPM](https://github.com/cpm-cmake/CPM.cmake) or
`add_subdirectory()`:

```cmake
CPMAddPackage(NAME base GITHUB_REPOSITORY timow-gh/base GIT_TAG v0.1.0
              OPTIONS "base_COMPONENTS memory")
target_link_libraries(my_app PRIVATE base::memory)
```

`base_COMPONENTS` lists the components to build; their dependencies are added
automatically. Empty builds all of them.

**Installed package**, from a release archive or `cmake --install`:

```cmake
find_package(base 0.1 CONFIG REQUIRED COMPONENTS memory)   # -DCMAKE_PREFIX_PATH=<package>
target_link_libraries(my_app PRIVATE base::memory)
```

## Building

Additionally requires Ninja on Linux or Visual Studio 2022 on Windows.

```sh
cmake --list-presets=all                               # presets available on this host
cmake --workflow --preset workflow-test-gcc-release    # configure, build and test
```

## License

Public domain, see [License](License).
