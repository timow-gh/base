# Container benchmarks

Compares the arena containers of `base::containers` with the same STL containers on
`std::allocator` and on `std::pmr::monotonic_buffer_resource`, using
[Google Benchmark](https://github.com/google/benchmark). Every benchmark is named
`<container>/<workload>/<implementation>/<elements>`, e.g. `map/insert_lookup/arena/4096`.

## Run

From the repository root, with Python 3 and CMake:

| Goal | Command | Time |
|---|---|---|
| Does everything still run? | `python benchmark/run.py canary` | ~1 s |
| Feedback while changing code | `python benchmark/run.py quick --container map` | ~1 min for all, seconds per container |
| The complete picture | `python benchmark/run.py full` | ~14 min |

The runner builds the release benchmark preset for your compiler (`msvc` on Windows,
`gcc` on Linux, `clang` on macOS; change it with `--compiler`), runs the benchmarks with
live output and writes the report. The first run also configures the build and
downloads Google Benchmark, which takes a few minutes once. Use `--no-build` to run
the executable as it is.

## Results

Every run gets its own folder, printed at the end of the run:

```
out/benchmarks/<compiler>/<date>_<time>_<suite>/          e.g. out/benchmarks/msvc/2026-10-17_143526_quick/
    report.html     the report: verdict per workload, charts, statistics; open it in a browser
    results.json    raw Google Benchmark output with every repetition
    console.txt     what the run printed
    metadata.json   descriptions of the workloads and implementations, read by the report
```

Runs with filters end in `_filtered`, and their report says which part of the suite
ran. Old runs stay until you delete them. The report needs internet access for its
charts (Chart.js from cdnjs); its tables work offline. To rebuild a report:

```sh
python benchmark/report/make_report.py out/benchmarks/msvc/<run>/results.json out/benchmarks/msvc/<run>/report.html
```

## Suites

| Suite | Sizes (elements) | Repetitions | Min time per repetition | Purpose |
|---|---|---|---|---|
| `canary` | 4096 | 1 | 0.01 s | Everything still runs, nothing is wildly off |
| `quick` (default) | 512, 32Ki, 256Ki | 3 | 0.05 s | Feedback while changing code |
| `full` | 64 to 1Mi in steps of 8× | 10 | 0.1 s | Conclusions, with tight confidence intervals |

The churn workloads stop at 128Ki elements. Quick and full shuffle all repetitions,
so CPU frequency or thermal drift spreads over every implementation instead of
biasing one. Run the full suite on an otherwise idle machine; the report warns about
noisy measurements, debug builds and enabled assertions or arena debug checks.

## Choose what runs

Filters take comma-separated names, combine with AND, and work with every suite:

```sh
python benchmark/run.py quick --container map                      # every map workload
python benchmark/run.py quick --container map,unordered_map --impl std,arena
python benchmark/run.py quick --workload churn                     # churn of every container
python benchmark/run.py quick --tag locality                       # the prebuilt-container workloads
python benchmark/run.py full --container vector --size 4096,65536  # own sizes instead of the suite's
python benchmark/run.py quick --container map --list               # show what would run, run nothing
python benchmark/run.py quick -- --benchmark_repetitions=5         # anything after -- goes to Google Benchmark
```

A misspelled name fails with the list of valid names. `--list` without filters shows
everything:

| Container | Workloads (tag) |
|---|---|
| `vector` | `push_back`, `push_back_reserved`, `short_strings`, `long_strings` (allocation) |
| `string` | `append` (allocation) |
| `map` | `insert_lookup` (allocation), `lookup_prebuilt`, `iterate_prebuilt` (locality), `churn` (churn) |
| `set` | `insert_contains` (allocation) |
| `unordered_map` | `insert_lookup`, `insert_lookup_reserved` (allocation), `lookup_prebuilt` (locality), `churn` (churn) |
| `unordered_set` | `insert_contains` (allocation) |

| Implementation | What it measures |
|---|---|
| `std` | The default heap (`operator new`); the baseline of every speedup |
| `pmr` | `std::pmr::monotonic_buffer_resource` over a buffer that fits one iteration, released after each |
| `arena` | One `Arena` for the run, committed up front, rewound by an `ArenaScope` after each iteration |
| `arena_cold` | A new `Arena` per iteration: reserve, commit, page faults and release are timed too |

The inputs come from fixed seeds without the standard library's distributions, so
every implementation and compiler sees the same keys and strings.

## Running the executable directly

The runner calls `base_container_benchmarks` from the build folder
(`out/build/conf-<compiler>-release-benchmark/bin/`), which takes the same selection
as `--name=value` flags and prints to the console only:

```sh
base_container_benchmarks --suite=canary --container=map --impl=std,arena
base_container_benchmarks --list
base_container_benchmarks --help    # these flags, then Google Benchmark's
```

## Adding to the suite

The suite is built so that new workloads and new containers plug in without touching
the rest:

- **A workload** is a struct in one of the `*_benchmarks.cpp` files with
  `using Container = MapKind;` (the container kind it needs), `name`, `tags`,
  `description`, `operation`, `maxElements` and `template <typename Implementation>
  static void run(benchmark::State&)`, which calls `run_workload` with a generic lambda
  over the container types (see `harness.hpp`). Add `specs_for<YourWorkload>(AllImplementations{})`
  to the file's list. It then runs with every implementation that provides that
  container.
- **An implementation**, e.g. your own flat map, is a struct in `implementations.hpp`
  with `name`, `description`, a `Containers` type set and `measure`, `time` and
  `with_source` (copy one of the existing ones). The type set provides only the
  containers it implements, e.g. just `Map`. Add it to `AllImplementations`; it then
  runs every workload of those containers and nothing else.
- **A container kind** is a struct in `registry.hpp` with a `name` and a
  `providedBy` check, added to `containerOrder`.

Descriptions reach the report through `--describe`, so the report needs no change.
