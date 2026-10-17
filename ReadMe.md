[![Ubuntu Build](https://github.com/timow-gh/base/actions/workflows/ubuntu-build.yml/badge.svg?branch=main)](https://github.com/timow-gh/base/actions/workflows/ubuntu-build.yml)
[![Windows Build](https://github.com/timow-gh/base/actions/workflows/windows-build.yml/badge.svg)](https://github.com/timow-gh/base/actions/workflows/windows-build.yml)

# base

base is a low-level foundation library for C++23: memory management, platform
abstractions and core utilities. It is split into components, and a project builds
and links only the components it uses.

The components are static libraries, or interface libraries when they are
header-only, with no third-party dependencies.

## Components

```
core  <──  platform  <──  memory  <──  containers
              ^
              └──  profile
```

| Component | Target | Content |
|---|---|---|
| `core` | `base::core` | Platform/compiler detection (`config.hpp`), assertions `BASE_ASSERT(cond)` / `BASE_ASSERT_MSG(cond, msg)` with a replaceable handler and no cost when disabled (`assert.hpp`), alignment helpers (`alignment.hpp`), byte units (`units.hpp`), ASan poisoning macros (`asan.hpp`), `atomic_fetch_max` (`atomic.hpp`) |
| `platform` | `base::platform` | Virtual memory: reserve, commit, decommit and release address space; heap allocation with any alignment and the usable size of a block (Win32 and POSIX) |
| `memory` | `base::memory` | `Arena`: a linear allocator over reserved virtual memory, and arena statistics |
| `containers` | `base::containers` | Header-only. STL containers that allocate from an `Arena`: `ArenaVector`, `ArenaString`, `ArenaMap`, `ArenaSet`, `ArenaUnorderedMap`, `ArenaUnorderedSet` |
| `profile` | `base::profile` | Profiling zones `BASE_PROFILE_SCOPE` / `BASE_PROFILE_FUNCTION` / `BASE_PROFILE_BANDWIDTH`, timed with the CPU timestamp counter or counting heap allocations, and compiled out unless profiling is enabled. `base::profile_global_new` replaces the global `operator new` in allocation builds to track every heap allocation |

Linking a component links its dependencies too. `base::base` links every
component that was built.

## Using base from another project

**From source** (recommended), with [CPM](https://github.com/cpm-cmake/CPM.cmake) or
`add_subdirectory()`:

```cmake
CPMAddPackage(NAME base GITHUB_REPOSITORY timow-gh/base GIT_TAG v0.1.0
              OPTIONS "base_COMPONENTS memory")   # core and platform are added automatically
target_link_libraries(my_app PRIVATE base::memory)
```

`base_COMPONENTS` is a list of components to build. Empty, the default, builds all of them.
An unknown name is a configure error.

**Installed SDK**, from a release archive or `cmake --install`:

```cmake
find_package(base 0.1 CONFIG REQUIRED COMPONENTS memory)   # -DCMAKE_PREFIX_PATH=<sdk>
target_link_libraries(my_app PRIVATE base::memory)
```

Until 1.0 a version request is satisfied only by the same minor version, because
minor versions may break the API. `find_package()` loads only the requested
components and their dependencies.
Without `COMPONENTS` it loads everything and also defines `base::base`. On Windows
the SDK contains both Debug (`base_cored.lib`, ...) and Release libraries, because
MSVC cannot link a Debug program against Release libraries.
`test/test_install/consumer/` is a complete example.

## The arena

```cpp
#include <base/memory/arena.hpp>

using base::memory::Arena;
using base::memory::arena_bytes_for;

std::optional<Arena> arena = Arena::create(base::mib(64));          // or Arena::create() for 64 GiB
Particle* particles = arena->push_array<Particle>(1000).data();

// Size an arena for exactly what it will hold:
constexpr std::size_t meshSize = arena_bytes_for<Vertex>(10'000) + arena_bytes_for<Index>(30'000);
std::optional<Arena> meshArena = Arena::create(meshSize);

{
    const base::memory::ArenaScope scratch(*arena);   // temporary allocations ...
    auto* buffer = static_cast<char*>(arena->allocate(4096, 16));
}                                                      // ... are freed here

arena->reset();                                        // frees everything, O(1)
```

The design decisions:

- **Reserve and commit.** `create()` reserves one contiguous range of address
  space (`ArenaConfig::reserveSize`, default 64 GiB). Reserving costs no
  memory. Physical memory is committed in `commitSize` steps (default 64 KiB) as
  the arena grows, so an arena only uses the memory it has handed out. The arena
  never moves, so pointers stay valid until the arena is rewound past them.
- **Sizing.** `arena_bytes_for<T>(count)` is the space `count` Ts need,
  including worst-case alignment padding. The sizes of several types add up to a
  safe total. Reservations are rounded up to the allocation granularity (64 KiB
  on Windows).
- **Linear, no destructors.** Allocation bumps an offset. Memory is never freed
  one allocation at a time. `reset()` frees everything, and `mark_position()` /
  `rewind_to()` or an `ArenaScope` free everything allocated after a point.
  Destructors never run, so `push<T>()` / `push_array<T>()` only accept trivially
  destructible types.
  `std::string` and similar types are a compile error.
- **STL containers through `ArenaAllocator`.** Containers run destructors, but
  their freed blocks are not reused either. They suit containers that are built,
  used and discarded together with the arena or an `ArenaScope`. A long-lived
  container that keeps erasing and inserting grows its arena for as long as it lives.
- **No zeroing on rewind.** `reset()` and `rewind_to()` only move the offset; the
  next user overwrites the memory anyway. `allocate_zeroed()` and `push_array()`
  clear only reused bytes. Memory that was never handed out is still zero from
  the OS.
- **Readable debug patterns.** In debug builds (`Arena::debug_checks_enabled()`),
  the arena fills memory with patterns you can recognize in a debugger:

  | Bytes | Meaning |
  |---|---|
  | `CD CD CD CD ...` | allocated with `allocate()` but not yet initialized |
  | `DD DD DD DD ...` | freed by `reset()` / `rewind_to()`: a pointer that shows this is dangling |
  | `00 00 00 00 ...` | committed but never used |

- **Use-after-free assertion.** Before the arena hands out memory again, debug
  builds check that the freed memory still holds `0xDD`. If something wrote
  there after it was freed, `BASE_ASSERT` fires: *"arena: memory at offset N was
  written after reset/rewind (use-after-free)"*.
- **AddressSanitizer.** Under ASan every byte that is not handed out is poisoned.
  Any read or write of freed memory, or past the last allocation, is reported as
  `use-after-poison`.
- **Not thread-safe.** Use one arena per thread.

The debug patterns follow `BASE_ENABLE_ASSERTS`, which is on unless `NDEBUG` is
defined. Define `BASE_ARENA_DEBUG=0/1` when building base to override them.

## Profiling

Mark the code you want to measure with zones. They can stay in the code for good:
unless profiling is enabled, they compile to nothing. Nothing else is needed: the
session starts with the first zone, and the report is written when the program
exits.

```cpp
#include <base/profile/profile.hpp>

void simulate(World& world) {
    BASE_PROFILE_FUNCTION();                                   // zone named "simulate"
    {
        BASE_PROFILE_SCOPE("broad phase");                     // times the rest of this scope
        build_grid(world);
    }
    BASE_PROFILE_BANDWIDTH("integrate", world.bodies.size_bytes());   // also reports GB/s
    integrate(world.bodies);
}
```

Configure with `-Dbase_PROFILING=TIME` and run the program. When it exits, the
report of a small demo program looks like this on stdout:

```
Total: 90.37 ms (CPU timer 3.80 GHz)
zone             hits             exclusive             inclusive      bandwidth
sum                50       83.20 ms  92.1%       83.20 ms  92.1%     19.23 GB/s
fib            418050        7.14 ms   7.9%        7.14 ms   7.9%
frame              50        0.02 ms   0.0%       90.37 ms 100.0%
fib phase          50        0.01 ms   0.0%        7.15 ms   7.9%
```

- **Enabling it.** Configure with `-Dbase_PROFILING=TIME` (or a `*-release-profile`
  preset), or define `BASE_ENABLE_PROFILING=1` yourself. It does not follow `NDEBUG`:
  profile optimized builds, because the timings of debug builds are misleading.
  `-Dbase_PROFILING=ALLOCATIONS` makes the same zones count heap allocations
  instead, see [Allocation tracking](#allocation-tracking).
- **Where the report goes.** To stdout by default. With
  `-Dbase_PROFILING_REPORT_DIRECTORY=profiles -Dbase_PROFILING_REPORT_NAME=game`
  it is written to `profiles/game-20261016T134512Z.txt` instead (UTC time, so
  runs do not overwrite each other), and one line on stdout names the file. The
  options set the macros `BASE_PROFILE_REPORT_DIRECTORY` and
  `BASE_PROFILE_REPORT_NAME`, documented in `profile.hpp`. The report is written
  when the program returns from `main()` or calls `std::exit()`, not after
  `std::abort()` or a crash.
- **Optional calls.** `base::profile::reset()` discards what was measured so far,
  e.g. after loading. `base::profile::report()` writes the report at a point you
  choose, and `print_report(stream)` writes it to a stream of your own. Both
  replace the report at exit.
- **Reading the report.** Zones are sorted by *exclusive* time, the time spent in
  the zone itself but not in zones nested in it. That is where to look for
  bottlenecks. *Inclusive* time includes nested zones; a recursive zone is
  counted once, for its outermost call. Bandwidth is the processed bytes divided
  by the inclusive time. Compare it with your machine's memory bandwidth to see
  whether a loop is limited by memory.
- **Accuracy.** Zones read the CPU's timestamp counter (`rdtsc` on x64,
  `cntvct_el0` on ARM64) on entry and exit. It is converted to milliseconds with
  the frequency measured against `steady_clock` over the session, so longer
  sessions convert more precisely. The cost is about two timer reads per zone:
  put zones around loops, not inside very hot ones.
- **Limits.** Only one thread may run zones. The profiler only sees the zones
  you marked, and it measures wall-clock time, including time when the OS
  pauses the thread. For "where is the time going at all?", use a sampling
  profiler.

## Allocation tracking

Where a program allocates is often where it loses time. base counts heap
allocations for the whole process and per profiling zone, and counts the
allocations and memory of its arenas.

**Heap allocations** are only visible through `operator new`, so tracking them
means replacing it, which slows every allocation down. Allocation tracking is
therefore a profiling mode of its own, exclusive with timing: configure with
`-Dbase_PROFILING=ALLOCATIONS` (or a `*-release-profile-allocations` preset), and
link `base::profile_global_new` to your executable.

```cmake
target_link_libraries(my_app PRIVATE base::profile base::profile_global_new)
```

`base::profile_global_new` replaces `operator new` only in allocation builds and
compiles to nothing in every other build, so it can stay linked: a `TIME` build
measures times undisturbed, an `ALLOCATIONS` build counts allocations.

```cpp
#include <base/profile/heap_tracking.hpp>

base::profile::reset_heap_peak();
run_frame();
const base::profile::HeapStats stats = base::profile::heap_stats();
// stats.allocationCount, allocatedBytes, freedBytes, liveBytes, peakLiveBytes
```

If your program already replaces `operator new` (or uses an allocator library
that does), leave `base::profile_global_new` out. Call
`base::profile::record_heap_allocation(bytes)` from your `operator new` and
`record_heap_deallocation(bytes)` with the same byte count from your
`operator delete`, both inside `#if BASE_PROFILE_ALLOCATIONS`; everything below
then works the same.

- **Per zone.** In an allocation build the zones count the heap allocations made
  in them instead of measuring time, and the report lists them instead of times,
  the zone with the most allocated bytes first. They are counted like exclusive
  time, not including nested zones, and only on the thread that runs the zones.
  A demo that builds 2000 strings per frame:

  ```
  Allocations in zones: 121200 (17682720 bytes)
  zone              hits      allocs     alloc bytes
  load_names          60      121200        17682720
  frame               60           0               0
  count_long          60           0               0
  ```

- **Why no times.** Tracking costs every `new` and `delete` a usable-size query
  and a few atomic operations on one cache line that every allocating thread
  writes. Times measured next to it would be distorted, so an allocation build
  reports none. Measure times with a `TIME` build.
- **Byte counts** are the usable sizes of the blocks, which can be a little
  larger than what was requested. That keeps `liveBytes` exact without a header
  on every block. Blocks freed through the replacement that it never saw
  allocated (on Windows, some blocks of the C++ runtime DLL) make `liveBytes`
  too low rather than wrap around.
- **Limits.** Allocations with `malloc()` are not seen. On Windows a
  replacement applies only to the module (exe or DLL) it is linked into. Under
  AddressSanitizer it takes over from ASan's `operator new`, so ASan no longer
  reports `new[]`/`delete` mismatches.

**Arenas** need no opt-in and are counted in every build, because it costs
nothing measurable: an arena counts its allocations in a plain member, and the
process totals change only when an arena commits. `Arena::allocation_count()`
and `peak_used()` show how an arena is used, and `peak_used()` is the right size
for its reservation or `ScratchConfig::initialCommit`.
`base::memory::arena_totals()` sums the reserved and committed memory of all
arenas of the process, including the scratch arenas.

## Building and testing

Prerequisites:
- CMake 3.28 or newer
- Ninja on Linux, Visual Studio 2022 on Windows
- GCC 14, Clang 20 or MSVC 19.40 or newer

Preset names follow `<step>-<compiler>-<config>[-<variant>]`:
- **steps:** `conf`, `build`, `test`, `workflow`
- **compilers:** `gcc`, `clang`, `msvc`
- **configs:** `debug`, `release`, `relwithdebinfo`, `minsizerel`
- **variants:** `-asan`, `-tsan`, `-ubsan` (Linux only), `-coverage` (`gcc-coverage`)

```sh
cmake --list-presets=all                                # everything available on this host
cmake --workflow --preset workflow-test-msvc-debug      # configure + build + test
cmake --workflow --preset workflow-test-clang-debug-asan
```

The tests:

| Test | What it checks |
|---|---|
| `base.<component>` | The unit tests of each component (GoogleTest), in `components/<name>/test/` |
| `TestbaseInstall` | Installs the SDK and builds `test/test_install/consumer/` with `find_package(base COMPONENTS containers profile)` |
| `TestbaseSubdirectory*` | Builds `test/test_subdirectory/consumer/` with `add_subdirectory()` and `base_COMPONENTS=core`, and fails if other components are built |

Options (the defaults in brackets apply when base is the top-level project; as a subproject they are all OFF):

| Option | Meaning |
|---|---|
| `base_COMPONENTS` | Components to build; empty builds all of them [empty] |
| `base_TESTS` | Build the unit tests and the consumer tests [ON] |
| `base_INSTALL` | Generate install rules [ON] |
| `base_USE_WARNINGS`, `base_WARNINGS_AS_ERRORS` | Compiler warnings, as errors [ON] |
| `base_STATIC_ANALYSIS` | clang-tidy (Clang) / MSVC code analysis [ON] |
| `base_BENCHMARKS` | Build the container benchmarks; `benchmark/run.py` configures this for you [OFF] |
| `base_PROFILING` | `OFF`, `TIME` or `ALLOCATIONS`, see [Profiling](#profiling) [OFF] |

The components are compiled as position-independent code unless the build sets
`CMAKE_POSITION_INDEPENDENT_CODE` itself.

## Benchmarks

`benchmark/` compares the arena containers with the same STL containers on
`std::allocator` and `std::pmr`. From the repository root:

```sh
python benchmark/run.py canary                    # ~1 s: does everything still run?
python benchmark/run.py quick --container map     # seconds to a minute: feedback while changing code
python benchmark/run.py full                      # ~14 min: the complete picture
python benchmark/compare.py main HEAD WORKTREE quick  # the same benchmarks on several commits, one report
```

Each run writes `report.html` and `results.json` to its own folder,
`out/benchmarks/<compiler>/<date>_<time>_<suite>/`, and prints the paths at the end.
See [benchmark/README.md](benchmark/README.md) for the filters, the suites, comparing
commits and how to add workloads or containers.

## Adding a component

1. Declare it in the root `CMakeLists.txt`, after its dependencies:
   `base_declare_component(strings DEPENDS core memory)`.
2. Create `components/strings/CMakeLists.txt` with one `base_add_component()` call
   (sources, headers) and one `base_add_component_test()` call. See
   `components/memory/CMakeLists.txt`. Without `SOURCES` the component is
   header-only and becomes an interface library; each header is then compiled
   on its own to get warnings and static analysis without passing them on to
   consumers.
3. Put public headers in `components/strings/include/base/strings/` and use
   `namespace base::strings`.

The declaration is the only place the dependency graph is written down. It
drives `base_COMPONENTS`, the links between targets, and the dependency files of
the installed package.

## Design decisions

- **Static only.** The components are small and low-level, and allocators should
  not cross DLL boundaries. Because they are compiled as PIC (by default, see
  `CMAKE_POSITION_INDEPENDENT_CODE`), consumers can still link them into their own
  shared libraries, which Linux requires. Header-only components
  have nothing to compile and are interface libraries.
- **No third-party dependencies.** Not even through the arena: assertion messages
  are formatted on the stack.
- **The MSVC runtime is the consumer's choice.** base doesn't set
  `CMAKE_MSVC_RUNTIME_LIBRARY`, so it uses CMake's default (`/MD`) or whatever the
  consuming project sets.
- **The template.** base started as a clone of
  [cpplib](https://github.com/timow-gh/cpplib), and the remote `template` still
  points there, so improvements to presets, warnings and CI can be cherry-picked.
  The application, packaging and delivery parts of the template were removed.

## Releases and CI

| Workflow | Purpose |
|---|---|
| `ubuntu-build.yml`, `windows-build.yml` | Build and test on every push and pull request (GCC, Clang, MSVC; debug and release) |
| `ubuntu-sanitizers.yml`, `ubuntu-coverage.yml`, `ubuntu-static-analysis.yml` | ASan/TSan/UBSan, coverage report, clang-tidy |
| `release.yml` | On a `v*` tag: checks the tag against the `project()` version, builds and tests the SDK on Linux and Windows, and publishes the archives in a GitHub Release with notes generated by git-cliff |
| `commit-messages.yml` | Enforces Conventional Commits |

See [CONTRIBUTING.md](CONTRIBUTING.md) for commit conventions and the release
procedure. This project is public domain, see [License](License).
