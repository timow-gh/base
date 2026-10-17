# Container benchmarks

Measures how fast the arena containers of `base::containers` are compared with the
same STL containers on the default heap (`std::allocator`) and on the standard
library's own arena (`std::pmr::monotonic_buffer_resource`), using
[Google Benchmark](https://github.com/google/benchmark). Every benchmark is named
`<container>/<workload>/<implementation>/<elements>`, e.g. `map/insert_lookup/arena/4096`:
a map, filled and searched, on an arena, with 4096 elements.

## Before you start

You need:

- Python 3 (nothing to install beyond it)
- CMake 3.28 or newer
- a C++23 compiler: Visual Studio 2022 with the C++ workload on Windows; GCC or Clang
  with Ninja on Linux and macOS
- git, for comparing commits
- internet access the first time, to download Google Benchmark

Run every command from the repository root. The first run configures the build and
downloads Google Benchmark, which takes a few minutes once; later runs only rebuild
what changed.

## Measure the current code

| Goal | Command | Time |
|---|---|---|
| Does everything still run? | `python benchmark/run.py canary` | ~1 s |
| Quick feedback while changing code | `python benchmark/run.py quick --container map` | ~1 min for all, seconds per container |
| Numbers you can draw conclusions from | `python benchmark/run.py full` | ~14 min |

`run.py` builds the code as it is in your working tree, runs the benchmarks with live
output and prints where the report is. Open `report.html` in a browser.

The word after `run.py` is the suite. It decides how much is measured:

| Suite | Sizes (elements) | Repetitions | Use it for |
|---|---|---|---|
| `canary` | 4096 | 1 | Checking that everything runs. Its numbers mean nothing |
| `quick` (default) | 512, 32Ki, 256Ki | 3 | Seeing a direction while you work |
| `full` | 64 to 1Mi in steps of 8× | 10 | Conclusions, and anything you tell others |

## Compare two commits

Use `compare.py` to find out whether a change made things faster or slower. It checks
out each commit, builds it and runs the same benchmarks on all of them, then writes
one page with the differences.

1. Commit your change. (To measure uncommitted changes instead, name them `WORKTREE`,
   see below.)
2. Close other programs, and plug a laptop in. Anything else that uses the CPU
   distorts the numbers.
3. Run, with the older commit first:
   ```sh
   python benchmark/compare.py HEAD~1 HEAD full
   ```
   This takes about twice the suite's time, ~30 min for `full`, plus a few minutes to
   build each commit the first time it is compared. Leave the machine alone meanwhile.
4. Open the `comparison.html` printed at the end, and read it as described in
   [Read the results](#read-the-results).

The first commit is the baseline: every other one is compared with it. Commits,
branches and tags all work:

| Question | Command |
|---|---|
| Did my last commit help? | `python benchmark/compare.py HEAD~1 HEAD full` |
| Is my branch faster than main? | `python benchmark/compare.py main HEAD full` |
| Do my uncommitted changes help? | `python benchmark/compare.py HEAD WORKTREE full` |
| Quick look at one container | `python benchmark/compare.py main HEAD quick --container vector` |
| How did several commits develop? | `python benchmark/compare.py v1.0 v1.1 main full` |
| Is a small difference real? | `python benchmark/compare.py main HEAD full --rounds 3` |

`compare.py` refuses commits older than 246cdbb, which can't build the benchmarks, and
two names for the same code, e.g. `WORKTREE` without changes next to `HEAD`.

## Read the results

Both pages state results as **speedups**: how many times faster something is than the
reference. `report.html` compares each implementation with `std::allocator`;
`comparison.html` compares each commit with the baseline commit.

- **Above 1× is faster, below is slower.** 1.25× takes 20 % less time; 0.80× takes 25 %
  more. Green and red mark changes of 5 % or more.
- **The range in brackets is the 95 % confidence interval**: where the true speedup
  lies, given how much the repetitions varied. A chip says at how many sizes the
  intervals separate, e.g. "faster at every size"; "no clear difference" means the
  measurements can't tell the two apart.
- **Check `std` and `pmr` first in a comparison.** They don't use base's allocators, so
  a commit can't change them: whatever they move is the machine, and the other rows
  need to move by more than that to mean something.
- **A real change shows at several sizes.** One size that jumps far up or down while its
  neighbours stay put is a disturbed measurement, not the code.
- **Noisy cells are marked in yellow**: their repetitions varied by more than 5 %
  (coefficient of variation, CV). Rerun when the cells you care about are noisy.
- **Yellow boxes at the top mean "don't trust this run"**, or only part of it: a
  debug build, assertions on, a single repetition, a filtered selection, or commits
  that ran under different conditions.
- **The commit is in the header.** `-dirty` after it means the library code had
  uncommitted changes when it ran.

## Getting trustworthy numbers

- Use `full` for anything you conclude or share; `quick` only shows a direction.
- Keep the machine idle: no builds, no video, no IDE indexing.
- On a laptop, stay on AC power and use the high performance power plan.
- Only compare numbers from the same machine and compiler. `compare.py` does that for
  you; numbers from different machines or days don't compare.
- If the controls (`std`, `pmr`) move by more than the change you're looking for, rerun
  with `--rounds 3`: the commits then take turns three times and the drift of the
  machine spreads over all of them, at three times the run time.

## Choose what runs

Both scripts take the same filters. They take comma-separated names, combine with
AND, and work with every suite:

```sh
python benchmark/run.py quick --container map                      # every map workload
python benchmark/run.py quick --container map,unordered_map --impl std,arena
python benchmark/run.py quick --workload churn                     # churn of every container
python benchmark/run.py quick --tag locality                       # the prebuilt-container workloads
python benchmark/run.py full --container vector --size 4096,65536  # own sizes instead of the suite's
python benchmark/run.py quick --container map --list               # show what would run, run nothing
```

Keep `std` in an `--impl` filter: it is the reference for every speedup in
`report.html`. A misspelled name fails with the list of valid names. `--list` without
filters shows everything:

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
| `std` | The default heap (`operator new`); the reference of every speedup |
| `pmr` | `std::pmr::monotonic_buffer_resource` over a buffer that fits one iteration, released after each |
| `arena` | One `Arena` for the run, committed up front, rewound by an `ArenaScope` after each iteration |
| `arena_cold` | A new `Arena` per iteration: reserve, commit, page faults and release are timed too |

Everything after `--` goes to Google Benchmark unchanged. The useful ones:

```sh
python benchmark/run.py quick -- --benchmark_repetitions=20       # more repetitions than the suite's
python benchmark/run.py quick -- --benchmark_min_time=0.5s        # longer per repetition
python benchmark/run.py quick -- --benchmark_filter=vector/push_back/arena/  # a regular expression on the names
```

The churn workloads stop at 128Ki elements. The inputs come from fixed seeds, so every
implementation, commit and compiler sees the same keys and strings.

## Reference

| Piece | What it does |
|---|---|
| `benchmark/run.py` | Builds the working tree, runs one selection, writes a report |
| `benchmark/compare.py` | Builds and runs several commits, writes one comparison |
| `benchmark/report/make_report.py` | Turns a run's `results.json` into `report.html` |
| `benchmark/report/make_comparison.py` | Turns a comparison folder into `comparison.html` |
| `base_container_benchmarks` | The benchmark executable, in `out/build/conf-<compiler>-release-benchmark/bin/` |
| `benchmark/runner.py` | Shared by the two scripts; not run directly |

| Option | `run.py` | `compare.py` | Meaning |
|---|---|---|---|
| `canary`, `quick`, `full` | ✓ | ✓ | The suite; `quick` when left out |
| `--container`, `--workload`, `--impl`, `--tag`, `--size` | ✓ | ✓ | Filters, see above |
| `--compiler msvc\|gcc\|clang` | ✓ | ✓ | Which build to use; `msvc` on Windows, `gcc` on Linux, `clang` on macOS by default |
| `--list` | ✓ | ✓ | Show what would run, then exit (`compare.py` builds the first commit for it) |
| `-- <arguments>` | ✓ | ✓ | Passed to Google Benchmark |
| `--no-build` | ✓ | | Run the executable as it is |
| `REV ...`, `WORKTREE` | | ✓ | The commits to compare, baseline first |
| `--rounds N` | | ✓ | Let the commits take turns N times |
| `--clean` | | ✓ | Delete the checkouts, then exit |

Every run gets its own folder, printed at the end; runs with filters end in `_filtered`:

```
out/benchmarks/<compiler>/<date>_<time>_<suite>/            from run.py
    report.html       open it in a browser
    results.json      raw Google Benchmark output with every repetition
    console.txt       what the run printed
    metadata.json     descriptions of the workloads and implementations, read by the report

out/benchmarks/<compiler>/<date>_<time>_compare_<suite>/    from compare.py
    comparison.html   open it in a browser
    comparison.json   the medians and speedups of every cell
    commits.json      the commits, their subjects and run times
    <short sha>/      one run per commit, as run.py writes it, with its own report.html
```

The charts need internet access (Chart.js from cdnjs); the tables work offline. To
rebuild a page, e.g. after changing the report scripts:

```sh
python benchmark/report/make_report.py <run folder>/results.json <run folder>/report.html
python benchmark/report/make_comparison.py <comparison folder>
```

Old runs stay until you delete their folders. `python benchmark/compare.py --clean`
deletes the commit checkouts in `out/compare/src/`.

## How compare.py builds the commits

Each commit is checked out once in `out/compare/src/<sha>/` and kept, so comparing it
again only rebuilds what changed. The library code comes from the commit, but the
`benchmark/` folder and `CMakePresets.json` come from your working tree, so every
commit runs the same workloads with the same compiler flags. This means that **edits
to `benchmark/` that you haven't committed apply to every compared commit**. Google
Benchmark is downloaded once into `out/compare/cpm-cache/` and shared.

Every commit is built before the first one runs, so no compiler competes with a
measurement. The commits then run one after the other; `quick` and `full` shuffle the
repetitions within each run. With `--rounds N` they take turns N times in rotating order, and each
commit's repetitions are merged.

## Running the executable directly

The scripts call `base_container_benchmarks` from the build folder
(`out/build/conf-<compiler>-release-benchmark/bin/`). It takes the same selection as
`--name=value` flags and prints to the console only, without a report:

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
