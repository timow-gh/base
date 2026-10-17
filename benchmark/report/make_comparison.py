"""Turns a folder written by benchmark/compare.py into comparison.html and comparison.json.

Usage: make_comparison.py <compare folder> [--fragment]

The folder holds commits.json and one run.py-style folder per commit. Every cell
(workload, implementation, size) of a later commit is compared with the same cell of
the first commit, the baseline. Standard library only.
"""

import argparse
import json
import sys
from pathlib import Path

from make_report import (CONTAINER_ORDER, NOISY_CV_PERCENT, Metadata, build_series, collect,
                         collect_warnings, color_tokens, context_rows, esc, format_count, format_duration,
                         format_ns, format_speedup, geomean, implementation_order, label, parse_name,
                         render_page, speedup_tone, swatch, verdict_chip, workload_title)

# Neither uses base's allocators, so whatever changes for them between commits is the
# machine, not the code: their change is the noise floor of the comparison.
CONTROLS = {"std", "pmr"}

# Context fields that must match for the commits' numbers to be comparable.
COMPARABLE_CONTEXT = ["host_name", "num_cpus", "mhz_per_cpu", "compiler", "base_build_type", "library_build_type",
                      "arena_debug_checks", "asserts", "asan", "suite", "filters", "sizes"]


def load_run(folder):
    results = json.loads((folder / "results.json").read_text(encoding="utf-8"))
    metadata_path = folder / "metadata.json"
    metadata = Metadata(json.loads(metadata_path.read_text(encoding="utf-8")) if metadata_path.exists() else {})
    return results, build_series(collect(results.get("benchmarks", []))), metadata


def change(base, point):
    """Speedup of a cell against the baseline commit's same cell, with bounds from the 95 % intervals."""
    return {"n": point["n"], "speedup": base["median"] / point["median"],
            "speedupLow": base["ciLow"] / point["ciHigh"], "speedupHigh": base["ciHigh"] / point["ciLow"],
            # A single repetition has no interval, and would separate from any other value.
            "hasInterval": base["reps"] > 1 and point["reps"] > 1}


def compare_series(baseline, other):
    """workload -> implementation -> changes per size, for the cells both commits measured."""
    changes = {}
    for key, series in other.items():
        for implementation, points in series.items():
            reference = {p["n"]: p for p in baseline.get(key, {}).get(implementation, [])}
            rated = [change(reference[p["n"]], p) for p in points if p["n"] in reference]
            if rated:
                changes.setdefault(key, {})[implementation] = rated
    return changes


def short_change(points):
    mean = geomean(p["speedup"] for p in points)
    chip = verdict_chip(points) if all(p["hasInterval"] for p in points) else ""
    return f'<span class="num {speedup_tone(mean)}"><b>{format_speedup(mean)}</b></span> {chip}'


def implementation_cell(implementation, tokens):
    control = ' <span class="chip chip-even">control</span>' if implementation in CONTROLS else ""
    return (f'<td class="left"><span class="variant-name">{swatch(tokens[implementation])}'
            f'{esc(label(implementation))}</span>{control}</td>')


def context_mismatches(contexts):
    mismatches = []
    for field in COMPARABLE_CONTEXT:
        values = {str(context.get(field)) for context in contexts}
        if len(values) > 1:
            mismatches.append(f"{field} ({' / '.join(sorted(values))})")
    return mismatches


def render_body(commits, runs, changes, ordered_keys, implementations, tokens):
    baseline_label = commits[0]["label"]
    later = commits[1:]
    contexts = [results.get("context", {}) for results, _, _ in runs]
    base_results, base_workloads, metadata = runs[0]
    parts = []

    commit_rows = "".join(
        f'<tr><td class="left mono">{esc(c["label"])}</td><td class="left">{"baseline" if i == 0 else ""}</td>'
        f'<td class="left">{esc(c["subject"])}</td><td class="left mono">{esc(c["date"][:16].replace("T", " "))}</td>'
        f'<td>{format_duration(c.get("elapsed"))}</td></tr>' for i, c in enumerate(commits))
    context = [(k, v) for k, v in context_rows(contexts[0], None, 0) if k not in ("Commit", "Run time", "Repetitions")]
    repetitions = min((p["reps"] for _, workloads, _ in runs for s in workloads.values() for ps in s.values()
                       for p in ps), default=0)
    context.insert(3, ("Repetitions per commit", repetitions))
    context_html = "".join(f"<div><dt>{esc(k)}</dt><dd>{esc(v)}</dd></div>" for k, v in context)
    parts.append(
        '<header class="intro"><span class="eyebrow">base · container benchmarks · commit comparison</span>'
        f"<h1>{esc(' vs '.join(c['label'] for c in commits))}</h1>"
        "<p>Every commit was built from its own library code with the same benchmark code and run on the same "
        f"machine, one after the other. Changes are speedups against the baseline <span class=\"mono\">"
        f"{esc(baseline_label)}</span>: above 1× is faster. std::allocator and pmr do not use base's allocators, "
        "so their change shows how much the machine itself drifted.</p>"
        f'<div class="table-wrap"><table><thead><tr><th class="left">Commit</th><th class="left"></th>'
        f'<th class="left">Subject</th><th class="left">Date</th><th>Run time</th></tr></thead>'
        f'<tbody>{commit_rows}</tbody></table></div>'
        f'<dl class="context">{context_html}</dl></header>')

    warnings = collect_warnings(contexts[0], base_workloads, implementations)
    mismatches = context_mismatches(contexts)
    if mismatches:
        warnings.insert(0, ("The commits ran under different conditions",
                            f"These differ between the runs: {'; '.join(mismatches)}. Their numbers do not compare."))
    for _, workloads, _ in runs[1:]:
        noisy = [p for s in workloads.values() for ps in s.values() for p in ps if p["cv"] > NOISY_CV_PERCENT]
        if noisy:
            warnings.append(("Noisy measurements in later commits too",
                             "Some cells of the other commits have a CV above "
                             f"{NOISY_CV_PERCENT:.0f} % as well; each commit's own report marks them."))
            break
    if warnings:
        items = "".join(f'<div class="warning"><strong>{esc(t)}.</strong> {esc(d)}</div>' for t, d in warnings)
        parts.append(f'<div class="warnings">{items}</div>')

    # Overview: one row per implementation, one column per later commit.
    head = "".join(f'<th class="left mono">{esc(c["label"])}</th>' for c in later)
    rows = []
    for implementation in implementations:
        cells = []
        for commit in later:
            points = [p for series in changes[commit["label"]].values() for p in series.get(implementation, [])]
            cells.append(f'<td class="left">{short_change(points)}</td>' if points else '<td class="left">–</td>')
        rows.append(f"<tr>{implementation_cell(implementation, tokens)}{''.join(cells)}</tr>")
    parts.append(
        '<section class="method" aria-labelledby="overview-title"><h2 id="overview-title">Change against '
        f'{esc(baseline_label)}</h2><p>Geometric mean speedup over every workload and size, and at how many cells '
        "the 95 % confidence intervals separate. A control that moves shows the noise to expect for the "
        "others.</p>"
        f'<div class="table-wrap"><table><thead><tr><th class="left">Implementation</th>{head}</tr></thead>'
        f'<tbody>{"".join(rows)}</tbody></table></div></section>')

    # Per workload: one row per workload, one column per (later commit, implementation).
    def column_commit(commit):
        return f'<br><span class="mono">{esc(commit["label"])}</span>' if len(later) > 1 else ""
    head = "".join(f'<th class="left"><span class="variant-name">{swatch(tokens[i])}{esc(label(i))}</span>'
                   f'{column_commit(c)}</th>' for c in later for i in implementations)
    rows = []
    for key in ordered_keys:
        cells = []
        for commit in later:
            for implementation in implementations:
                points = changes[commit["label"]].get(key, {}).get(implementation)
                cells.append(f'<td class="left">{short_change(points)}</td>' if points else '<td class="left">–</td>')
        rows.append(f'<tr><td class="left"><a href="#{esc(key)}">{esc(workload_title(key))}</a></td>'
                    f'{"".join(cells)}</tr>')
    parts.append(
        '<section class="method" aria-labelledby="workloads-title"><h2 id="workloads-title">Change per workload'
        "</h2><p>Geometric mean speedup over the sizes of each workload.</p>"
        f'<div class="table-wrap"><table><thead><tr><th class="left">Workload</th>{head}</tr></thead>'
        f'<tbody>{"".join(rows)}</tbody></table></div></section>')

    for key in ordered_keys:
        present = [i for i in implementations if i in base_workloads.get(key, {})]
        parts.append(
            f'<section class="workload" id="{esc(key)}"><div class="workload-head">'
            f'<span class="eyebrow">{esc(key.split("/", 1)[0])}</span><h2>{esc(workload_title(key))}</h2>'
            f'<p>{esc(metadata.workload(key, "description"))}</p></div><div class="charts">'
            '<figure class="chart"><h3>Change by size</h3><div class="chart-box">'
            f'<canvas data-workload="{esc(key)}" data-chart="sizes" role="img" '
            f'aria-label="Speedup by size against {esc(baseline_label)}"></canvas></div>'
            f'<figcaption class="chart-note">Speedup of {esc(later[-1]["label"])} against {esc(baseline_label)}; the '
            "band spans the ratio of the 95 % confidence bounds. Above the dashed line is faster.</figcaption></figure>"
            '<figure class="chart"><h3>Change across commits</h3><div class="chart-box">'
            f'<canvas data-workload="{esc(key)}" data-chart="commits" role="img" '
            f'aria-label="Speedup across commits against {esc(baseline_label)}"></canvas></div>'
            '<figcaption class="chart-note">Geometric mean speedup over the sizes, per commit.</figcaption>'
            "</figure></div>"
            f'<details><summary>All numbers for {esc(workload_title(key))}</summary>'
            f"{render_workload_table(key, commits, runs, changes, present, tokens)}</details></section>")
    return "\n".join(parts)


def render_workload_table(key, commits, runs, changes, implementations, tokens):
    head = "".join(f'<th class="mono">{esc(c["label"])}</th>' for c in commits)
    head += "".join(f'<th class="left">{esc(c["label"])} vs {esc(commits[0]["label"])}</th>' for c in commits[1:])
    rows = []
    for implementation in implementations:
        sizes = sorted({p["n"] for _, workloads, _ in runs for p in workloads.get(key, {}).get(implementation, [])})
        for size in sizes:
            cells = []
            for _, workloads, _ in runs:
                point = next((p for p in workloads.get(key, {}).get(implementation, []) if p["n"] == size), None)
                if point is None:
                    cells.append("<td>–</td>")
                    continue
                noisy = ' class="noisy"' if point["cv"] > NOISY_CV_PERCENT else ""
                cells.append(f'<td{noisy} title="CV {point["cv"]:.1f} %">{format_ns(point["median"])}</td>')
            for commit in commits[1:]:
                point = next((p for p in changes[commit["label"]].get(key, {}).get(implementation, [])
                              if p["n"] == size), None)
                cells.append("<td class=\"left\">–</td>" if point is None else
                             f'<td class="left"><span class="{speedup_tone(point["speedup"])}">'
                             f'{format_speedup(point["speedup"])}</span> <span class="muted">'
                             f'({point["speedupLow"]:.2f}–{point["speedupHigh"]:.2f})</span></td>')
            rows.append(f"<tr>{implementation_cell(implementation, tokens)}<td>{format_count(size)}</td>"
                        f'{"".join(cells)}</tr>')
    return (f'<div class="table-wrap"><table><thead><tr><th class="left">Implementation</th><th>n</th>{head}</tr>'
            f'</thead><tbody>{"".join(rows)}</tbody></table></div>')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("folder", type=Path)
    parser.add_argument("--fragment", action="store_true",
                        help="write the page without <html>, <head> and <body>, for hosts that add them")
    arguments = parser.parse_args()

    manifest = json.loads((arguments.folder / "commits.json").read_text(encoding="utf-8"))
    commits = manifest["commits"]
    if len(commits) < 2:
        sys.exit(f"{arguments.folder}: a comparison needs at least two commits")
    runs = [load_run(arguments.folder / commit["label"]) for commit in commits]
    baseline = runs[0][1]
    changes = {commit["label"]: compare_series(baseline, workloads)
               for commit, (_, workloads, _) in zip(commits[1:], runs[1:])}

    def order(key):
        container = key.split("/", 1)[0]
        rank = CONTAINER_ORDER.index(container) if container in CONTAINER_ORDER else len(CONTAINER_ORDER)
        return rank, container
    # Stable: within a container, workloads keep the order in which they ran.
    first_seen = list(dict.fromkeys(
        f"{p[0]}/{p[1]}" for p in (parse_name(b.get("run_name", b["name"])) for b in runs[0][0]["benchmarks"]) if p))
    ordered_keys = sorted((k for k in first_seen if k in baseline), key=order)
    implementations = implementation_order({i for series in baseline.values() for i in series})
    tokens = color_tokens(implementations)

    body = render_body(commits, runs, changes, ordered_keys, implementations, tokens)
    later = [commit["label"] for commit in commits[1:]]
    chart_data = {
        "labels": {i: label(i) for i in implementations},
        "colorTokens": tokens,
        "commits": [commit["label"] for commit in commits],
        "last": later[-1],
        "workloads": [
            {"key": key,
             "sizes": sorted({p["n"] for points in baseline[key].values() for p in points}),
             "implementations": [i for i in implementations if i in baseline[key]],
             # Per implementation: the last commit's change by size, and the change per commit.
             "bySize": {i: changes[later[-1]].get(key, {}).get(i, []) for i in implementations},
             "byCommit": {i: [1.0] + [geomean(p["speedup"] for p in changes[c].get(key, {}).get(i, [])) for c in later]
                          for i in implementations if i in baseline[key]}}
            for key in ordered_keys
        ],
    }
    page = render_page("comparison_template.html", body, chart_data, arguments.fragment)
    (arguments.folder / "comparison.html").write_text(page, encoding="utf-8")

    summary = {
        "baseline": commits[0]["label"],
        "commits": commits,
        "cells": [
            {"workload": key, "implementation": implementation, "n": point["n"],
             "medianNs": {commit["label"]: next((p["median"] for p in workloads.get(key, {}).get(implementation, [])
                                                 if p["n"] == point["n"]), None)
                          for commit, (_, workloads, _) in zip(commits, runs)},
             "speedup": {commit_label: next(({"value": p["speedup"], "low": p["speedupLow"], "high": p["speedupHigh"]}
                                       for p in changes[commit_label].get(key, {}).get(implementation, [])
                                       if p["n"] == point["n"]), None)
                         for commit_label in later}}
            for key in ordered_keys for implementation in implementations
            for point in baseline[key].get(implementation, [])
        ],
    }
    (arguments.folder / "comparison.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    print(f"wrote {arguments.folder / 'comparison.html'} ({len(ordered_keys)} workloads, "
          f"{len(summary['cells'])} cells)")


if __name__ == "__main__":
    main()
