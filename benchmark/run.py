"""Builds and runs the container benchmarks, then writes the HTML report.

    python benchmark/run.py [canary|quick|full] [--container A,B] [--workload A,B] [--impl A,B]
                            [--tag A,B] [--size N,M] [--compiler msvc|gcc|clang] [--no-build] [--list]
                            [-- <Google Benchmark arguments>]

Each run gets its own folder, out/benchmarks/<compiler>/<date>_<time>_<suite>/, with
report.html, results.json, console.txt and metadata.json; the paths are printed at the
end. Standard
library only. See benchmark/README.md.
"""

import argparse
import platform
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXECUTABLE = "base_container_benchmarks"
SUITES = ["canary", "quick", "full"]
FILTERS = ["container", "workload", "impl", "tag", "size"]


def default_compiler():
    system = platform.system()
    if system == "Windows":
        return "msvc"
    if system == "Darwin":
        return "clang"
    return "gcc"


def parse_arguments():
    # Everything after "--" goes to Google Benchmark unchanged.
    argv = sys.argv[1:]
    passthrough = []
    if "--" in argv:
        split = argv.index("--")
        argv, passthrough = argv[:split], argv[split + 1:]

    parser = argparse.ArgumentParser(
        description="Build and run the container benchmarks, then write the HTML report.",
        epilog="Filters take comma-separated names and combine with AND; --list shows what they select. "
               "Arguments after -- go to Google Benchmark, e.g. -- --benchmark_repetitions=5.")
    parser.add_argument("suite", nargs="?", default="quick", choices=SUITES,
                        help="canary: seconds; quick: about a minute (default); full: the complete picture")
    parser.add_argument("--container", help="only these containers, e.g. map,vector")
    parser.add_argument("--workload", help="only these workloads, e.g. insert_lookup,churn")
    parser.add_argument("--impl", help="only these implementations, e.g. std,arena")
    parser.add_argument("--tag", help="only workloads with one of these tags: allocation, locality, churn")
    parser.add_argument("--size", help="these element counts instead of the suite's, e.g. 4096,65536")
    parser.add_argument("--compiler", choices=["msvc", "gcc", "clang"], default=default_compiler(),
                        help=f"which benchmark preset to build and run (default here: {default_compiler()})")
    parser.add_argument("--no-build", action="store_true", help="run the executable as it is, without building")
    parser.add_argument("--list", action="store_true", help="show what would run, then exit")
    arguments = parser.parse_args(argv)
    return arguments, passthrough


def run_step(command):
    print("> " + " ".join(str(part) for part in command), flush=True)
    if subprocess.run(command, cwd=ROOT).returncode != 0:
        sys.exit(f"failed: {' '.join(str(part) for part in command)}")


def build(compiler):
    preset = f"{compiler}-release-benchmark"
    if not (ROOT / "out" / "build" / f"conf-{preset}").exists():
        run_step(["cmake", "--preset", f"conf-{preset}"])
    run_step(["cmake", "--build", "--preset", f"build-{preset}", "--target", EXECUTABLE])


def find_executable(compiler):
    build_dir = ROOT / "out" / "build" / f"conf-{compiler}-release-benchmark"
    # bin/Release/ for Visual Studio, bin/ for Ninja.
    candidates = [path for path in build_dir.glob(f"bin/**/{EXECUTABLE}*")
                  if path.is_file() and path.suffix in ("", ".exe")]
    if not candidates:
        sys.exit(f"{EXECUTABLE} not found in {build_dir}; run without --no-build")
    return max(candidates, key=lambda path: path.stat().st_mtime)


def benchmark_arguments(arguments, passthrough):
    selection = [f"--suite={arguments.suite}"]
    for name in FILTERS:
        value = getattr(arguments, name)
        if value:
            selection.append(f"--{name}={value}")
    return selection + passthrough


def main():
    arguments, passthrough = parse_arguments()
    if not arguments.no_build:
        build(arguments.compiler)
    executable = find_executable(arguments.compiler)
    selection = benchmark_arguments(arguments, passthrough)

    # Checks the names before creating a run folder, and shows what will run.
    preview = subprocess.run([executable, *selection, "--list"], capture_output=True, text=True)
    if preview.returncode != 0:
        sys.exit(preview.stderr.strip() or preview.stdout.strip())
    if arguments.list:
        print(preview.stdout, end="")
        return
    print(preview.stdout.strip().splitlines()[-1], flush=True)

    filtered = any(getattr(arguments, name) for name in FILTERS) or any(
        argument.startswith("--benchmark_filter") for argument in passthrough)
    stamp = datetime.now().strftime("%Y-%m-%d_%H%M%S")
    folder = ROOT / "out" / "benchmarks" / arguments.compiler / f"{stamp}_{arguments.suite}{'_filtered' if filtered else ''}"
    folder.mkdir(parents=True)
    results = folder / "results.json"
    report = folder / "report.html"
    print(f"Results go to {folder}\n", flush=True)
    # Descriptions for the report, which looks for metadata.json next to results.json.
    describe = subprocess.run([executable, "--describe"], capture_output=True, text=True, check=True)
    (folder / "metadata.json").write_text(describe.stdout, encoding="utf-8")

    command = [executable, *selection, f"--benchmark_out={results}", "--benchmark_out_format=json"]
    # The console shows one line per benchmark; results.json keeps every repetition.
    if arguments.suite != "canary" and not any(a.startswith("--benchmark_display_aggregates_only") for a in passthrough):
        command.append("--benchmark_display_aggregates_only=true")
    started = time.monotonic()
    with open(folder / "console.txt", "w", encoding="utf-8") as console:
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                              encoding="utf-8", errors="replace") as process:
            for line in process.stdout:
                sys.stdout.write(line)
                console.write(line)
            process.wait()
    elapsed = time.monotonic() - started
    if process.returncode != 0:
        sys.exit(f"the benchmarks failed with exit code {process.returncode}; output in {folder / 'console.txt'}")

    run_step([sys.executable, str(Path(__file__).parent / "report" / "make_report.py"), str(results), str(report),
              "--elapsed", f"{elapsed:.1f}"])
    minutes, seconds = divmod(round(elapsed), 60)
    print(f"\nFinished in {minutes} min {seconds} s.")
    print(f"Report:  {report}")
    print(f"Results: {results}")


if __name__ == "__main__":
    main()
