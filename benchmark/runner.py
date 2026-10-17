"""Building, selecting and running the container benchmarks, shared by run.py and compare.py.

Every function takes the source tree it works on, so compare.py can build and run other
commits checked out next to this one. Standard library only.
"""

import platform
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXECUTABLE = "base_container_benchmarks"
SUITES = ["canary", "quick", "full"]
FILTERS = ["container", "workload", "impl", "tag", "size"]
REPORT_SCRIPT = Path(__file__).resolve().parent / "report" / "make_report.py"
# compare.py replaces these with today's in every commit it builds.
OVERLAID = ["benchmark", "CMakePresets.json"]
# The code a benchmark measures; changes anywhere else, e.g. in the docs, don't make a tree dirty.
LIBRARY_PATHS = ["components", "cmake", "dependencies", "CMakeLists.txt"]


def default_compiler():
    system = platform.system()
    if system == "Windows":
        return "msvc"
    if system == "Darwin":
        return "clang"
    return "gcc"


def split_passthrough(argv):
    """Everything after "--" goes to Google Benchmark unchanged."""
    if "--" in argv:
        split = argv.index("--")
        return argv[:split], argv[split + 1:]
    return argv, []


def add_selection_arguments(parser):
    parser.add_argument("--container", help="only these containers, e.g. map,vector")
    parser.add_argument("--workload", help="only these workloads, e.g. insert_lookup,churn")
    parser.add_argument("--impl", help="only these implementations, e.g. std,arena")
    parser.add_argument("--tag", help="only workloads with one of these tags: allocation, locality, churn")
    parser.add_argument("--size", help="these element counts instead of the suite's, e.g. 4096,65536")
    parser.add_argument("--compiler", choices=["msvc", "gcc", "clang"], default=default_compiler(),
                        help=f"which benchmark preset to build and run (default here: {default_compiler()})")
    parser.add_argument("--list", action="store_true", help="show what would run, then exit")


def selection_arguments(arguments, passthrough):
    selection = [f"--suite={arguments.suite}"]
    for name in FILTERS:
        value = getattr(arguments, name)
        if value:
            selection.append(f"--{name}={value}")
    return selection + passthrough


def is_filtered(arguments, passthrough):
    return any(getattr(arguments, name) for name in FILTERS) or any(
        argument.startswith("--benchmark_filter") for argument in passthrough)


def run_step(command, root=ROOT, env=None):
    print("> " + " ".join(str(part) for part in command), flush=True)
    if subprocess.run(command, cwd=root, env=env).returncode != 0:
        sys.exit(f"failed: {' '.join(str(part) for part in command)}")


def git(*arguments, root=ROOT):
    return subprocess.run(["git", *arguments], cwd=root, capture_output=True, text=True,
                          check=True).stdout.strip()


def commit_label(root=ROOT):
    """The short sha of HEAD, with -dirty when library code differs from it."""
    label = git("rev-parse", "--short", "HEAD", root=root)
    if git("status", "--porcelain", "--", *LIBRARY_PATHS, root=root):
        label += "-dirty"
    return label


def build_folder(compiler, root=ROOT):
    return root / "out" / "build" / f"conf-{compiler}-release-benchmark"


def build(compiler, root=ROOT, env=None):
    preset = f"{compiler}-release-benchmark"
    if not build_folder(compiler, root).exists():
        run_step(["cmake", "--preset", f"conf-{preset}"], root, env)
    run_step(["cmake", "--build", "--preset", f"build-{preset}", "--target", EXECUTABLE], root, env)


def find_executable(compiler, root=ROOT):
    folder = build_folder(compiler, root)
    # bin/Release/ for Visual Studio, bin/ for Ninja.
    candidates = [path for path in folder.glob(f"bin/**/{EXECUTABLE}*")
                  if path.is_file() and path.suffix in ("", ".exe")]
    if not candidates:
        sys.exit(f"{EXECUTABLE} not found in {folder}; run without --no-build")
    return max(candidates, key=lambda path: path.stat().st_mtime)


def preview(executable, selection):
    """The --list output for the selection; exits with the executable's message on a bad name."""
    result = subprocess.run([executable, *selection, "--list"], capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(result.stderr.strip() or result.stdout.strip())
    return result.stdout


def write_metadata(executable, folder):
    """Descriptions for the report, which looks for metadata.json next to results.json."""
    describe = subprocess.run([executable, "--describe"], capture_output=True, text=True, check=True)
    (folder / "metadata.json").write_text(describe.stdout, encoding="utf-8")


def run_benchmarks(executable, selection, results, console, commit):
    """Runs the selection with live output, appended to console, and returns the seconds it took."""
    command = [executable, *selection, f"--benchmark_context=base_commit={commit}",
               f"--benchmark_out={results}", "--benchmark_out_format=json"]
    # The console shows one line per benchmark; results.json keeps every repetition.
    if "--suite=canary" not in selection and not any(
            argument.startswith("--benchmark_display_aggregates_only") for argument in selection):
        command.append("--benchmark_display_aggregates_only=true")
    started = time.monotonic()
    with open(console, "a", encoding="utf-8") as log:
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                              encoding="utf-8", errors="replace") as process:
            for line in process.stdout:
                sys.stdout.write(line)
                log.write(line)
            process.wait()
    if process.returncode != 0:
        sys.exit(f"the benchmarks failed with exit code {process.returncode}; output in {console}")
    return time.monotonic() - started


def make_report(results, report, elapsed):
    run_step([sys.executable, str(REPORT_SCRIPT), str(results), str(report), "--elapsed", f"{elapsed:.1f}"])


def format_minutes(seconds):
    minutes, rest = divmod(round(seconds), 60)
    return f"{minutes} min {rest} s"
