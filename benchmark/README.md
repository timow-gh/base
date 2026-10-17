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

## Compare commits

`compare.py` runs the same selection on several commits and puts them side by side:

```sh
python benchmark/compare.py HEAD~1 HEAD full                 # did the last commit make a difference?
python benchmark/compare.py main HEAD WORKTREE quick --container vector
python benchmark/compare.py v1.0 main --rounds 3             # alternate the commits three times
python benchmark/compare.py --clean                          # remove the checkouts
```

The first revision is the baseline; any commit, branch or tag works, and `WORKTREE`
is the working tree with its uncommitted changes. The suite and the filters are those
of `run.py`. Each commit is checked out once in `out/compare/src/<sha>/` and built
there with the commit's own library code, but with today's `benchmark/` folder and
`CMakePresets.json`, so every commit runs the same workloads with the same flags. That
works from 246cdbb, the first commit that builds the benchmarks; older ones are
refused. Checkouts stay for incremental rebuilds, and Google Benchmark is downloaded
once into `out/compare/cpm-cache/`.

Every commit is built before the first one runs. Then the commits run one after the
other; with `--rounds N` they take turns N times in rotating order and their
repetitions are merged, which spreads drift of the machine over all of them at N times
the run time. The results go to one folder:

```
out/benchmarks/<compiler>/<date>_<time>_compare_<suite>/
    comparison.html   speedup of every commit against the baseline, per implementation,
                      workload and size, with 95 % confidence intervals
    comparison.json   the medians and speedups of every cell
    commits.json      the commits, their subjects and run times
    <short sha>/      one run as run.py writes it, with its own report.html
```

`std` and `pmr` don't use base's allocators, so their change between commits is the
machine, not the code: read it as the noise floor. To rebuild the page, run
`python benchmark/report/make_comparison.py <folder>`.

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
