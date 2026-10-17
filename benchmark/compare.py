"""Benchmarks several commits with today's benchmark code and writes one comparison report.

    python benchmark/compare.py REV [REV ...] [canary|quick|full] [--container A,B] [--workload A,B]
                                [--impl A,B] [--tag A,B] [--size N,M] [--compiler msvc|gcc|clang]
                                [--rounds N] [--list] [--clean] [-- <Google Benchmark arguments>]

The first REV is the baseline; WORKTREE is the working tree with its uncommitted changes.
Each commit is checked out in out/compare/src/<sha>/, where today's benchmark/ folder and
CMakePresets.json replace the commit's, so every commit runs the same workloads with the
same flags and only the library code differs. The results go to
out/benchmarks/<compiler>/<date>_<time>_compare_<suite>/ with comparison.html, one folder
per commit as run.py writes it, and commits.json. Standard library only. See
benchmark/README.md.
"""

import argparse
import filecmp
import json
import os
import shutil
import subprocess
import sys
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

import runner

WORKTREE = "WORKTREE"
COMPARE_ROOT = runner.ROOT / "out" / "compare"
SOURCES = COMPARE_ROOT / "src"
COMPARISON_SCRIPT = Path(__file__).resolve().parent / "report" / "make_comparison.py"


@dataclass
class Commit:
    rev: str
    sha: str
    label: str
    subject: str
    date: str
    root: str
    elapsed: float = 0.0


def parse_arguments():
    argv, passthrough = runner.split_passthrough(sys.argv[1:])
    parser = argparse.ArgumentParser(
        description="Benchmark several commits with today's benchmark code and compare them in one report.",
        epilog="The first revision is the baseline; WORKTREE is the working tree with uncommitted changes. "
               "Filters work as in run.py. Arguments after -- go to Google Benchmark.")
    parser.add_argument("revisions", nargs="*", metavar="REV",
                        help=f"commits, branches or tags, then optionally the suite ({', '.join(runner.SUITES)}; "
                             "default quick)")
    runner.add_selection_arguments(parser)
    parser.add_argument("--rounds", type=int, default=1,
                        help="run every commit this many times, in rotating order, and merge the repetitions; "
                             "spreads drift of the machine over all commits at the cost of run time (default 1)")
    parser.add_argument("--clean", action="store_true",
                        help="remove the checkouts in out/compare/src/, then exit")
    arguments = parser.parse_args(argv)

    # The suite is a positional too; a branch called "full" would need refs/heads/full.
    arguments.suite = "quick"
    revisions = []
    for revision in arguments.revisions:
        if revision in runner.SUITES:
            arguments.suite = revision
        else:
            revisions.append(revision)
    arguments.revisions = revisions
    if not arguments.clean and len(revisions) < 2 and not (arguments.list and revisions):
        parser.error("name at least two revisions to compare")
    if arguments.rounds < 1:
        parser.error("--rounds must be at least 1")
    return arguments, passthrough


def resolve(revision):
    if revision == WORKTREE:
        sha = runner.git("rev-parse", "HEAD")
        label = runner.commit_label()
        subject = runner.git("log", "-1", "--format=%s", "HEAD")
        return Commit(revision, sha, label, f"working tree on {subject}", datetime.now().astimezone().isoformat(
            timespec="seconds"), str(runner.ROOT))

    found = subprocess.run(["git", "rev-parse", "--verify", "--quiet", f"{revision}^{{commit}}"], cwd=runner.ROOT,
                           capture_output=True, text=True)
    if found.returncode != 0:
        sys.exit(f"unknown revision: {revision}")
    sha = found.stdout.strip()
    # The benchmarks build through the base_BENCHMARKS option of the root CMakeLists.txt.
    if "add_subdirectory(benchmark)" not in runner.git("show", f"{sha}:CMakeLists.txt"):
        sys.exit(f"{revision} ({sha[:7]}) predates the base_BENCHMARKS option (246cdbb); "
                 "its library cannot be built with the benchmarks")
    subject, date = runner.git("log", "-1", "--format=%s%x00%cI", sha).split("\0")
    return Commit(revision, sha, runner.git("rev-parse", "--short", sha), subject, date,
                  str(SOURCES / sha[:12]))


def mirror(source, target):
    """Makes target hold exactly source's files, touching only those that differ, so builds stay incremental."""
    target.mkdir(parents=True, exist_ok=True)
    comparison = filecmp.dircmp(source, target, ignore=["__pycache__"])
    for name in comparison.right_only:
        path = target / name
        if path.is_dir():
            shutil.rmtree(path)
        else:
            path.unlink()
    for name in comparison.left_only + comparison.diff_files + comparison.funny_files:
        path = source / name
        if path.is_dir():
            shutil.copytree(path, target / name, ignore=shutil.ignore_patterns("__pycache__"))
        else:
            shutil.copy2(path, target / name)
    for name in comparison.common_dirs:
        mirror(source / name, target / name)


def check_out(commit):
    root = Path(commit.root)
    if not (root / ".git").exists():
        # A checkout deleted by hand stays registered until pruned.
        runner.git("worktree", "prune")
        print(f"> git worktree add --detach {root} {commit.label}", flush=True)
        runner.git("worktree", "add", "--detach", str(root), commit.sha)
    for name in runner.OVERLAID:
        source, target = runner.ROOT / name, root / name
        if source.is_dir():
            mirror(source, target)
        elif not target.exists() or not filecmp.cmp(source, target, shallow=False):
            shutil.copy2(source, target)


def build_environment(commit):
    if commit.rev == WORKTREE:
        return None
    # One Google Benchmark download for every checkout instead of one each.
    environment = dict(os.environ)
    environment.setdefault("CPM_SOURCE_CACHE", str(COMPARE_ROOT / "cpm-cache"))
    return environment


def clean():
    if SOURCES.exists():
        for root in sorted(SOURCES.iterdir()):
            print(f"> git worktree remove --force {root}", flush=True)
            removed = subprocess.run(["git", "worktree", "remove", "--force", str(root)], cwd=runner.ROOT)
            if removed.returncode != 0 and root.exists():
                shutil.rmtree(root)
    runner.git("worktree", "prune")
    print("Removed the checkouts; the shared download cache in out/compare/cpm-cache stays.")


def merge_rounds(round_files, results):
    merged = None
    for path in round_files:
        data = json.loads(path.read_text(encoding="utf-8"))
        if merged is None:
            merged = data
        else:
            merged["benchmarks"].extend(data["benchmarks"])
        path.unlink()
    results.write_text(json.dumps(merged, indent=1), encoding="utf-8")


def main():
    arguments, passthrough = parse_arguments()
    if arguments.clean:
        clean()
        return

    commits = [resolve(revision) for revision in arguments.revisions]
    labels = [commit.label for commit in commits]
    duplicates = sorted({label for label in labels if labels.count(label) > 1})
    if duplicates:
        sys.exit(f"the same code is named twice: {', '.join(duplicates)}")
    selection = runner.selection_arguments(arguments, passthrough)

    # Everything is built before anything runs, so no compiler competes with a measurement.
    executables = []
    for index, commit in enumerate(commits):
        print(f"\n== {commit.label}  {commit.subject}", flush=True)
        if commit.rev != WORKTREE:
            check_out(commit)
        runner.build(arguments.compiler, Path(commit.root), build_environment(commit))
        executables.append(runner.find_executable(arguments.compiler, Path(commit.root)))
        if index == 0:
            # Today's benchmark code everywhere: one check of the names covers every commit.
            listing = runner.preview(executables[0], selection)
            if arguments.list:
                print(listing, end="")
                return
    print("\n" + listing.strip().splitlines()[-1], flush=True)

    filtered = runner.is_filtered(arguments, passthrough)
    stamp = datetime.now().strftime("%Y-%m-%d_%H%M%S")
    folder = (runner.ROOT / "out" / "benchmarks" / arguments.compiler /
              f"{stamp}_compare_{arguments.suite}{'_filtered' if filtered else ''}")
    folder.mkdir(parents=True)
    print(f"Results go to {folder}", flush=True)
    for commit, executable in zip(commits, executables):
        (folder / commit.label).mkdir()
        runner.write_metadata(executable, folder / commit.label)

    round_files = {commit.label: [] for commit in commits}
    for round_index in range(arguments.rounds):
        # Rotating the order keeps one commit from always running first, on a cooler machine.
        shift = round_index % len(commits)
        order = list(zip(commits, executables))[shift:] + list(zip(commits, executables))[:shift]
        for commit, executable in order:
            print(f"\n== round {round_index + 1} of {arguments.rounds}: {commit.label}  {commit.subject}\n",
                  flush=True)
            results = folder / commit.label / f"results_round{round_index + 1}.json"
            commit.elapsed += runner.run_benchmarks(executable, selection, results,
                                                    folder / commit.label / "console.txt", commit.label)
            round_files[commit.label].append(results)

    for commit in commits:
        results = folder / commit.label / "results.json"
        merge_rounds(round_files[commit.label], results)
        runner.make_report(results, folder / commit.label / "report.html", commit.elapsed)
    (folder / "commits.json").write_text(
        json.dumps({"suite": arguments.suite, "rounds": arguments.rounds, "compiler": arguments.compiler,
                    "commits": [asdict(commit) for commit in commits]}, indent=2), encoding="utf-8")
    runner.run_step([sys.executable, str(COMPARISON_SCRIPT), str(folder)])

    print(f"\nFinished in {runner.format_minutes(sum(commit.elapsed for commit in commits))} of benchmarks.")
    print(f"Comparison: {folder / 'comparison.html'}")


if __name__ == "__main__":
    main()
