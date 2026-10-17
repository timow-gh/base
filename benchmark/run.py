"""Builds and runs the container benchmarks, then writes the HTML report.

    python benchmark/run.py [canary|quick|full] [--container A,B] [--workload A,B] [--impl A,B]
                            [--tag A,B] [--size N,M] [--compiler msvc|gcc|clang] [--no-build] [--list]
                            [-- <Google Benchmark arguments>]

Each run gets its own folder, out/benchmarks/<compiler>/<date>_<time>_<suite>/, with
report.html, results.json, console.txt and metadata.json; the paths are printed at the
end. Standard library only. See benchmark/README.md.
"""

import argparse
import sys
from datetime import datetime

import runner


def parse_arguments():
    argv, passthrough = runner.split_passthrough(sys.argv[1:])
    parser = argparse.ArgumentParser(
        description="Build and run the container benchmarks, then write the HTML report.",
        epilog="Filters take comma-separated names and combine with AND; --list shows what they select. "
               "Arguments after -- go to Google Benchmark, e.g. -- --benchmark_repetitions=5.")
    parser.add_argument("suite", nargs="?", default="quick", choices=runner.SUITES,
                        help="canary: seconds; quick: about a minute (default); full: the complete picture")
    runner.add_selection_arguments(parser)
    parser.add_argument("--no-build", action="store_true", help="run the executable as it is, without building")
    return parser.parse_args(argv), passthrough


def main():
    arguments, passthrough = parse_arguments()
    if not arguments.no_build:
        runner.build(arguments.compiler)
    executable = runner.find_executable(arguments.compiler)
    selection = runner.selection_arguments(arguments, passthrough)

    # Checks the names before creating a run folder, and shows what will run.
    listing = runner.preview(executable, selection)
    if arguments.list:
        print(listing, end="")
        return
    print(listing.strip().splitlines()[-1], flush=True)

    filtered = runner.is_filtered(arguments, passthrough)
    stamp = datetime.now().strftime("%Y-%m-%d_%H%M%S")
    folder = (runner.ROOT / "out" / "benchmarks" / arguments.compiler /
              f"{stamp}_{arguments.suite}{'_filtered' if filtered else ''}")
    folder.mkdir(parents=True)
    results = folder / "results.json"
    report = folder / "report.html"
    print(f"Results go to {folder}\n", flush=True)
    runner.write_metadata(executable, folder)

    elapsed = runner.run_benchmarks(executable, selection, results, folder / "console.txt", runner.commit_label())
    runner.make_report(results, report, elapsed)
    print(f"\nFinished in {runner.format_minutes(elapsed)}.")
    print(f"Report:  {report}")
    print(f"Results: {results}")


if __name__ == "__main__":
    main()
