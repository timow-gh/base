"""Turns the Google Benchmark JSON of base_container_benchmarks into one HTML report.

Usage: make_report.py results.json report.html [--elapsed SECONDS] [--metadata FILE] [--fragment]

Benchmarks are named <container>/<workload>/<implementation>/<elements>. The
descriptions of workloads and implementations come from the executable's --describe
output (metadata.json), so new ones need no change here. The statistics use the raw repetitions (run with
--benchmark_report_aggregates_only=false). Standard library only. --fragment leaves
out the <!doctype html>, <head> and <body> wrapper, for hosts that add their own.
"""

import argparse
import html
import json
import math
import statistics
import sys
from pathlib import Path

BASELINE = "std"

# Display names and colors of the implementations this repository has; others show
# their own name and take the remaining colors in order.
LABELS = {"std": "std::allocator", "pmr": "pmr monotonic", "arena": "Arena (warm)", "arena_cold": "Arena (cold)"}
COLOR_TOKENS = {"std": "--v-std", "pmr": "--v-pmr", "arena": "--v-arena", "arena_cold": "--v-arena-cold"}
SPARE_COLOR_TOKENS = ["--v-extra-1", "--v-extra-2", "--v-extra-3"]

CONTAINER_ORDER = ["vector", "string", "map", "set", "unordered_map", "unordered_set"]

# Two-sided 95 % quantiles of Student's t for 1 to 30 degrees of freedom.
T_95 = [12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228, 2.201, 2.179, 2.160, 2.145,
        2.131, 2.120, 2.110, 2.101, 2.093, 2.086, 2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048,
        2.045, 2.042]

NOISY_CV_PERCENT = 5.0
TIME_UNIT_NS = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}


# --- statistics ---------------------------------------------------------------------

def percentile(sorted_values, fraction):
    if len(sorted_values) == 1:
        return sorted_values[0]
    position = fraction * (len(sorted_values) - 1)
    lower = math.floor(position)
    upper = min(lower + 1, len(sorted_values) - 1)
    return sorted_values[lower] + (sorted_values[upper] - sorted_values[lower]) * (position - lower)


def summarize(samples):
    ordered = sorted(samples)
    count = len(ordered)
    mean = statistics.fmean(ordered)
    stddev = statistics.stdev(ordered) if count > 1 else 0.0
    t_value = T_95[count - 2] if 2 <= count <= 31 else 1.96
    half_width = t_value * stddev / math.sqrt(count) if count > 1 else 0.0
    return {
        "reps": count,
        "median": statistics.median(ordered),
        "mean": mean,
        "stddev": stddev,
        "cv": 100.0 * stddev / mean if mean > 0 else 0.0,
        "min": ordered[0],
        "max": ordered[-1],
        "p5": percentile(ordered, 0.05),
        "p95": percentile(ordered, 0.95),
        # Kept above zero: speedup intervals divide by it.
        "ciLow": max(mean - half_width, 1e-12),
        "ciHigh": mean + half_width,
    }


def geomean(values):
    values = [v for v in values if v and v > 0]
    if not values:
        return None
    return math.exp(statistics.fmean(math.log(v) for v in values))


# --- parsing ------------------------------------------------------------------------

def parse_name(run_name):
    """<container>/<workload>/<implementation>/<elements>[/real_time] -> parts, or None."""
    parts = run_name.split("/")
    if parts and parts[-1] == "real_time":
        parts = parts[:-1]
    if len(parts) != 4:
        return None
    container, workload, implementation, size = parts
    try:
        return container, workload, implementation, int(size)
    except ValueError:
        return None


def collect(benchmarks):
    """Groups the raw repetitions by (container/workload, implementation, n)."""
    cells = {}
    for entry in benchmarks:
        if entry.get("run_type") != "iteration" or entry.get("error_occurred"):
            continue
        parsed = parse_name(entry.get("run_name", entry["name"]))
        if parsed is None or entry.get("items_per_second", 0) <= 0:
            continue
        container, workload, implementation, size = parsed
        key = (f"{container}/{workload}", implementation, size)
        cell = cells.setdefault(key, {"nsPerOp": [], "nsPerIteration": [], "counters": entry})
        cell["nsPerOp"].append(1e9 / entry["items_per_second"])
        cell["nsPerIteration"].append(entry["real_time"] * TIME_UNIT_NS.get(entry.get("time_unit", "ns"), 1.0))
    return cells


def build_series(cells):
    """workload -> implementation -> points sorted by n, with speedups against std."""
    workloads = {}
    for (workload, implementation, size), cell in cells.items():
        counters = cell["counters"]
        point = {"n": size, "samples": cell["nsPerOp"]}
        point.update(summarize(cell["nsPerOp"]))
        point["nsPerIteration"] = statistics.median(cell["nsPerIteration"])
        point["itemsPerSecond"] = 1e9 / point["median"]
        for key, name in [("peak_bytes", "peakBytes"), ("allocated_bytes", "allocatedBytes"),
                          ("bytes_per_element", "bytesPerElement"), ("allocations", "allocations")]:
            point[name] = counters.get(key)
        checkpoints = []
        while f"checkpoint_{len(checkpoints)}" in counters:
            checkpoints.append(counters[f"checkpoint_{len(checkpoints)}"])
        point["checkpoints"] = checkpoints
        workloads.setdefault(workload, {}).setdefault(implementation, []).append(point)

    for series in workloads.values():
        for points in series.values():
            points.sort(key=lambda p: p["n"])
        baseline = {p["n"]: p for p in series.get(BASELINE, [])}
        for points in series.values():
            for point in points:
                reference = baseline.get(point["n"])
                if reference is None:
                    point["speedup"] = point["speedupLow"] = point["speedupHigh"] = None
                    continue
                point["speedup"] = reference["median"] / point["median"]
                point["speedupLow"] = reference["ciLow"] / point["ciHigh"]
                point["speedupHigh"] = reference["ciHigh"] / point["ciLow"]
    return workloads


class Metadata:
    """Descriptions of workloads and implementations, from the executable's --describe."""

    def __init__(self, data):
        self.workloads = data.get("workloads", {})
        self.implementations = data.get("implementations", {})

    def workload(self, key, field, default=""):
        return self.workloads.get(key, {}).get(field, default)

    def tags(self, key):
        return [t for t in self.workload(key, "tags").split(",") if t]

    def implementation(self, name):
        return self.implementations.get(name, "")


def implementation_order(names):
    known = [n for n in LABELS if n in names]
    return known + sorted(n for n in names if n not in LABELS)


def label(name):
    return LABELS.get(name, name)


def color_tokens(implementations):
    tokens = {}
    spare = iter(SPARE_COLOR_TOKENS)
    for name in implementations:
        tokens[name] = COLOR_TOKENS.get(name) or next(spare, "--muted")
    return tokens


# --- formatting ---------------------------------------------------------------------

def esc(text):
    return html.escape(str(text), quote=True)


def format_ns(ns):
    if ns is None:
        return "–"
    if ns >= 1e6:
        return f"{ns / 1e6:.3g} ms"
    if ns >= 1e3:
        return f"{ns / 1e3:.3g} µs"
    return f"{ns:.3g} ns"


def format_bytes(value):
    if value is None:
        return "–"
    units = ["B", "KiB", "MiB", "GiB"]
    unit = 0
    while value >= 1024 and unit < len(units) - 1:
        value /= 1024
        unit += 1
    return f"{value:.3g} {units[unit]}"


def format_count(count):
    if count >= 1 << 20 and count % (1 << 20) == 0:
        return f"{count >> 20}Mi"
    if count >= 1 << 10 and count % (1 << 10) == 0:
        return f"{count >> 10}Ki"
    return str(count)


def format_duration(seconds):
    if seconds is None:
        return "–"
    minutes, rest = divmod(round(seconds), 60)
    return f"{minutes} min {rest} s" if minutes else f"{rest} s"


def format_speedup(value):
    return "–" if value is None else f"{value:.2f}×"


def speedup_tone(value):
    if value is None:
        return ""
    if value >= 1.05:
        return "tone-good"
    if value <= 0.95:
        return "tone-bad"
    return ""


def verdict_chip(points):
    """At how many sizes the confidence intervals separate from std."""
    rated = [p for p in points if p["speedup"] is not None]
    total = len(rated)
    faster = sum(1 for p in rated if p["speedupLow"] > 1.0)
    slower = sum(1 for p in rated if p["speedupHigh"] < 1.0)
    if total == 0:
        return ""
    if faster == total:
        return '<span class="chip chip-good">faster at every size</span>'
    if slower == total:
        return '<span class="chip chip-bad">slower at every size</span>'
    if faster and not slower:
        return f'<span class="chip chip-good">faster at {faster} of {total}</span>'
    if slower and not faster:
        return f'<span class="chip chip-bad">slower at {slower} of {total}</span>'
    if faster or slower:
        return f'<span class="chip chip-even">{faster} faster, {slower} slower of {total}</span>'
    return '<span class="chip chip-even">no clear difference</span>'


def swatch(token):
    return f'<span class="swatch" style="background: var({token})"></span>'


def workload_title(key):
    container, workload = key.split("/", 1)
    return f"{container} {workload.replace('_', ' ')}"


# --- page parts ---------------------------------------------------------------------

def context_rows(context, elapsed, repetitions):
    def cache(entry):
        instances = context.get("num_cpus", 1) // max(entry.get("num_sharing", 1), 1)
        return f"L{entry['level']} {entry['type'].lower()} {format_bytes(entry['size'])} ×{instances}"

    return [
        ("Suite", context.get("suite", "–")),
        ("Filters", context.get("filters", "–")),
        ("Sizes", ", ".join(format_count(int(s)) for s in context.get("sizes", "").split(",") if s) or "–"),
        ("Repetitions", repetitions or "–"),
        ("Run time", format_duration(elapsed)),
        ("Date", context.get("date", "–")),
        ("CPU", f"{context.get('num_cpus', '–')} logical cores at {context.get('mhz_per_cpu', '–')} MHz"),
        ("Caches", ", ".join(cache(entry) for entry in context.get("caches", [])) or "–"),
        ("Compiler", context.get("compiler", "–")),
        ("Build type", context.get("base_build_type", "–")),
        ("Commit", context.get("base_commit", "–")),
        ("Arena debug checks / asserts / ASan",
         f"{context.get('arena_debug_checks', '–')} / {context.get('asserts', '–')} / {context.get('asan', '–')}"),
    ]


def collect_warnings(context, workloads, implementations):
    warnings = []
    if context.get("filters", "none") != "none":
        warnings.append(("Partial run",
                         f"Only the benchmarks selected by {context['filters']} ran. The headline figures and the "
                         "verdict cover just those."))
    if implementations and BASELINE not in implementations:
        warnings.append(("No std::allocator baseline",
                         "The implementation filter left out std, so there are no speedups. Add --impl std."))
    if str(context.get("base_build_type", "")).lower() not in ("release", "relwithdebinfo"):
        warnings.append(("Not an optimized build",
                         f"base was built as {context.get('base_build_type', 'unknown')}. "
                         "benchmark/run.py uses the release presets."))
    if context.get("library_build_type") == "debug":
        warnings.append(("Google Benchmark is a debug build", "Its timing overhead is larger than in release."))
    if context.get("arena_debug_checks") == "on":
        warnings.append(("Arena debug checks are on",
                         "Every arena allocation is filled with a pattern, which slows the arena implementations."))
    if context.get("asserts") == "on":
        warnings.append(("Assertions are on", "BASE_ASSERT checks run inside the measured code."))
    if context.get("asan") == "on":
        warnings.append(("AddressSanitizer is on", "Every allocator pays for poisoning and shadow memory."))
    if context.get("cpu_scaling_enabled"):
        warnings.append(("CPU frequency scaling is on",
                         "Times can drift with the clock speed. Random interleaving spreads the drift over all "
                         "implementations, but the spread per cell grows."))
    cells = [p for series in workloads.values() for points in series.values() for p in points]
    noisy = [p for p in cells if p["cv"] > NOISY_CV_PERCENT]
    if noisy:
        warnings.append((f"{len(noisy)} of {len(cells)} measurements are noisy",
                         f"Their coefficient of variation is above {NOISY_CV_PERCENT:.0f} %; they are marked in "
                         "the tables. Close other programs and rerun for tighter numbers."))
    repetitions = min((p["reps"] for p in cells), default=0)
    if repetitions == 1:
        warnings.append(("Single repetition",
                         "Without repetitions there is no spread or confidence interval: fine for a canary run, "
                         "not for conclusions. Use the quick or full suite."))
    elif 0 < repetitions < 5:
        warnings.append(("Few repetitions",
                         f"Only {repetitions} repetitions per measurement; confidence intervals are wide. "
                         "The full suite uses 10."))
    return warnings


def headline_tiles(workloads, metadata):
    def with_tag(tag):
        return [k for k in workloads if tag in metadata.tags(k)]

    def speedup(implementation, keys, versus=None):
        values = []
        for key in keys:
            series = workloads[key]
            if implementation not in series:
                continue
            reference = {p["n"]: p for p in series.get(versus, [])} if versus else None
            for point in series[implementation]:
                if versus is None:
                    values.append(point["speedup"])
                elif point["n"] in reference:
                    values.append(reference[point["n"]]["median"] / point["median"])
        return geomean(values)

    tiles = []

    def add(tile_label, value, detail):
        if value is not None:
            tiles.append({"label": tile_label, "value": format_speedup(value), "detail": detail,
                          "tone": speedup_tone(value)})

    allocation = with_tag("allocation")
    add("Arena vs std::allocator", speedup("arena", allocation),
        "geometric mean speedup over every size of the allocation workloads")
    add("Arena vs pmr monotonic", speedup("arena", allocation, versus="pmr"),
        "the same workloads against the standard library's own arena")
    add("Arena vs std::allocator, locality", speedup("arena", with_tag("locality")),
        "lookups and traversal on prebuilt containers: node placement only")

    reserved = workloads.get("vector/push_back_reserved", {})
    if "arena" in reserved and "arena_cold" in reserved:
        warm_point, cold_point = reserved["arena"][0], reserved["arena_cold"][0]
        if warm_point["n"] == cold_point["n"]:
            overhead = cold_point["nsPerIteration"] - warm_point["nsPerIteration"]
            tiles.append({"label": "Cost of a fresh arena", "value": format_ns(overhead), "tone": "tone-warn",
                          "detail": f"extra time per iteration of the cold arena at n = {cold_point['n']}: "
                                    "reserve, commit, page faults and release"})

    churn = workloads.get("map/churn", {})
    if "arena" in churn and BASELINE in churn:
        arena_point, std_point = churn["arena"][-1], churn[BASELINE][-1]
        if arena_point.get("peakBytes") and std_point.get("peakBytes"):
            ratio = arena_point["peakBytes"] / std_point["peakBytes"]
            tiles.append({"label": "Arena memory under churn", "value": f"{ratio:.1f}×", "tone": "tone-bad",
                          "detail": f"peak bytes vs std::allocator after 8n erase + insert on a map of "
                                    f"n = {format_count(arena_point['n'])}"})
    return tiles


def render_summary(workloads, ordered_keys, metadata, compared, tokens):
    head = "".join(f'<th class="left"><span class="variant-name">{swatch(tokens[i])}{esc(label(i))}</span></th>'
                   for i in compared)
    rows = []
    for key in ordered_keys:
        series = workloads[key]
        cells = []
        for implementation in compared:
            points = series.get(implementation)
            rated = [p for p in points or [] if p["speedup"] is not None]
            if not rated:
                cells.append('<td class="left">–</td>')
                continue
            mean = geomean(p["speedup"] for p in rated)
            low = min(p["speedup"] for p in rated)
            high = max(p["speedup"] for p in rated)
            cells.append(
                f'<td class="left"><span class="num {speedup_tone(mean)}"><b>{format_speedup(mean)}</b></span> '
                f'<span class="num muted">({low:.2f}–{high:.2f})</span><br>{verdict_chip(rated)}</td>')
        memory = "–"
        if series.get("arena") and series.get(BASELINE):
            arena_peak, std_peak = series["arena"][-1].get("peakBytes"), series[BASELINE][-1].get("peakBytes")
            if arena_peak and std_peak:
                memory = f"{arena_peak / std_peak:.2f}×"
        tags = " ".join(f'<span class="chip chip-even">{esc(t)}</span>' for t in metadata.tags(key))
        rows.append(f'<tr><td class="left"><a href="#{esc(key)}">{esc(workload_title(key))}</a> {tags}</td>'
                    f'{"".join(cells)}<td class="num">{memory}</td></tr>')
    return (f'<div class="table-wrap"><table><thead><tr><th class="left">Workload</th>{head}'
            f'<th>Arena memory vs std at largest n</th></tr></thead><tbody>{"".join(rows)}</tbody></table></div>')


def render_workload_table(series, implementations, tokens):
    rows = []
    for implementation in implementations:
        for p in series[implementation]:
            noisy = ' class="noisy"' if p["cv"] > NOISY_CV_PERCENT else ""
            speedup = "–" if implementation == BASELINE or p["speedup"] is None else (
                f'<span class="{speedup_tone(p["speedup"])}">{format_speedup(p["speedup"])}</span> '
                f'<span class="muted">({p["speedupLow"]:.2f}–{p["speedupHigh"]:.2f})</span>')
            allocations = "–" if not p.get("allocations") else f'{int(p["allocations"]):,}'
            rows.append(
                f'<tr><td class="left"><span class="variant-name">{swatch(tokens[implementation])}'
                f'{esc(label(implementation))}</span></td><td>{format_count(p["n"])}</td>'
                f'<td><b>{format_ns(p["median"])}</b></td>'
                f'<td>{format_ns(p["mean"])} ± {format_ns(p["mean"] - p["ciLow"])}</td>'
                f'<td{noisy}>{p["cv"]:.1f} %</td><td>{format_ns(p["min"])}</td><td>{format_ns(p["max"])}</td>'
                f'<td>{format_ns(p["p5"])} – {format_ns(p["p95"])}</td><td>{speedup}</td>'
                f'<td>{p["itemsPerSecond"] / 1e6:,.1f} M/s</td><td>{format_bytes(p.get("bytesPerElement"))}</td>'
                f'<td>{format_bytes(p.get("peakBytes"))}</td><td>{allocations}</td><td>{p["reps"]}</td></tr>')
    head = ('<th class="left">Implementation</th><th>n</th><th>Median</th><th>Mean ± 95% CI</th><th>CV</th>'
            "<th>Min</th><th>Max</th><th>p5 – p95</th><th>Speedup vs std</th><th>Throughput</th>"
            "<th>Bytes / element</th><th>Peak bytes</th><th>Allocations</th><th>Reps</th>")
    return f'<div class="table-wrap"><table><thead><tr>{head}</tr></thead><tbody>{"".join(rows)}</tbody></table></div>'


def render_body(context, workloads, ordered_keys, metadata, implementations, tokens, elapsed):
    repetitions = max((p["reps"] for s in workloads.values() for ps in s.values() for p in ps), default=0)
    warnings = collect_warnings(context, workloads, implementations)
    tiles = headline_tiles(workloads, metadata)
    compared = [i for i in implementations if i != BASELINE]
    parts = []

    context_html = "".join(f"<div><dt>{esc(k)}</dt><dd>{esc(v)}</dd></div>"
                           for k, v in context_rows(context, elapsed, repetitions))
    parts.append(
        '<header class="intro"><span class="eyebrow">base · container benchmarks</span>'
        f"<h1>Arena container benchmarks</h1>"
        "<p>The STL containers of <span class=\"mono\">base::containers</span> allocate from an "
        "<span class=\"mono\">Arena</span> through <span class=\"mono\">ArenaAllocator</span>. This report compares "
        "them with the same containers on the default heap and on the standard library's monotonic arena, on "
        "synthetic data that is identical for every implementation. Lower times and higher speedups are better.</p>"
        f'<dl class="context">{context_html}</dl></header>')

    if warnings:
        items = "".join(f'<div class="warning"><strong>{esc(t)}.</strong> {esc(d)}</div>' for t, d in warnings)
        parts.append(f'<div class="warnings">{items}</div>')

    if tiles:
        tile_html = "".join(
            f'<div class="tile"><span class="tile-label">{esc(t["label"])}</span>'
            f'<span class="tile-value {t["tone"]}">{esc(t["value"])}</span>'
            f'<span class="tile-detail">{esc(t["detail"])}</span></div>' for t in tiles)
        parts.append(f'<section class="tiles" aria-label="Headline figures">{tile_html}</section>')

    legend = "".join(
        f'<div class="legend-item">{swatch(tokens[i])}<b>{esc(label(i))}</b>'
        f'<span class="desc">{esc(metadata.implementation(i))}</span></div>' for i in implementations)
    summary = render_summary(workloads, ordered_keys, metadata, compared, tokens) if compared else ""
    parts.append(
        '<section class="method" aria-labelledby="summary-title"><h2 id="summary-title">Verdict per workload</h2>'
        "<p>Geometric mean speedup over all sizes against std::allocator, the range across sizes in brackets, and "
        "at how many sizes the 95 % confidence intervals separate. Memory compares the peak bytes held at the "
        "largest size.</p>"
        f'<div class="legend">{legend}</div>{summary}</section>')

    containers = []
    for key in ordered_keys:
        container = key.split("/", 1)[0]
        if container not in containers:
            containers.append(container)
    jump = []
    for container in containers:
        links = " ".join(f'<a href="#{esc(k)}">{esc(k.split("/", 1)[1].replace("_", " "))}</a>'
                         for k in ordered_keys if k.startswith(container + "/"))
        jump.append(f'<span><span class="group">{esc(container)}</span> {links}</span>')
    parts.append(f'<nav class="jump" aria-label="Workloads">{"".join(jump)}</nav>')

    for key in ordered_keys:
        series = workloads[key]
        container = key.split("/", 1)[0]
        present = [i for i in implementations if i in series]
        operation = metadata.workload(key, "operation", "one operation")
        has_speedup = any(p["speedup"] is not None for i in present if i != BASELINE for p in series[i])
        has_memory = any(p.get("bytesPerElement") for i in present for p in series[i])
        has_churn = any(p["checkpoints"] for i in present for p in series[i])
        charts = [("time", "Time per operation", f"Median and min–max band. One operation is {operation}.")]
        if has_speedup:
            charts.append(("speedup", "Speedup vs std::allocator",
                           "Median ratio; the band spans the ratio of the 95 % confidence bounds. Above the dashed "
                           "line is faster."))
        if has_churn:
            charts.append(("churn", "Memory held while churning",
                           "Bytes held after filling (0n) and after every n operations, at the largest size."))
        elif has_memory:
            charts.append(("memory", "Peak memory per element",
                           "Largest amount held at once: live bytes for std::allocator, everything allocated for "
                           "the arena and pmr. The cold arena matches the warm one."))
        charts.append(("spread", "Spread of the repetitions",
                       "Every repetition at the largest size; a tight cluster means a stable measurement."))
        chart_html = "".join(
            f'<figure class="chart"><h3>{esc(name)}</h3><div class="chart-box">'
            f'<canvas data-workload="{esc(key)}" data-chart="{kind}" role="img" '
            f'aria-label="{esc(name)} for {esc(workload_title(key))}"></canvas></div>'
            f'<figcaption class="chart-note">{esc(note)}</figcaption></figure>'
            for kind, name, note in charts)
        tags = " ".join(f'<span class="chip chip-even">{esc(t)}</span>' for t in metadata.tags(key))
        parts.append(
            f'<section class="workload" id="{esc(key)}"><div class="workload-head">'
            f'<span class="eyebrow">{esc(container)}</span><h2>{esc(workload_title(key))} {tags}</h2>'
            f'<p>{esc(metadata.workload(key, "description"))}</p></div>'
            f'<div class="charts">{chart_html}</div>'
            f'<details><summary>All numbers for {esc(workload_title(key))}</summary>'
            f'{render_workload_table(series, present, tokens)}</details></section>')

    parts.append(
        '<section class="method workload" id="method"><h2>How the numbers were made</h2><ul>'
        "<li>Each benchmark runs a workload in a timed loop; Google Benchmark picks the iteration count. With "
        "several repetitions, the order of all repetitions is shuffled.</li>"
        "<li>Time per operation is wall-clock time divided by the operations of one iteration. Every iteration "
        "builds the containers, uses them and destroys them, so construction and teardown are included.</li>"
        "<li>The statistics use the raw repetitions: median, mean with a 95 % confidence interval (Student's t), "
        f"standard deviation as coefficient of variation (CV, marked above {NOISY_CV_PERCENT:.0f} %), min, max and "
        "the 5th to 95th percentile.</li>"
        "<li>Memory comes from one untimed pass per benchmark and size: a counting allocator for std::allocator, a "
        "counting memory resource for pmr, and Arena::used() for the arena, which includes alignment padding.</li>"
        "<li>The inputs are generated from fixed seeds, without the standard library's distributions, so every "
        "implementation, run and compiler sees the same keys and strings.</li>"
        "<li>Rerun with <span class=\"mono\">python benchmark/run.py "
        f"{esc(context.get('suite', 'quick'))}</span> on an otherwise idle machine; see benchmark/README.md.</li>"
        "</ul></section>")
    return "\n".join(parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("results", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--elapsed", type=float, help="wall-clock seconds of the run, shown in the header")
    parser.add_argument("--metadata", type=Path,
                        help="output of base_container_benchmarks --describe (default: metadata.json next to the "
                             "results); without it the report shows names only")
    parser.add_argument("--fragment", action="store_true",
                        help="write the page without <html>, <head> and <body>, for hosts that add them")
    arguments = parser.parse_args()

    results = json.loads(arguments.results.read_text(encoding="utf-8"))
    context = results.get("context", {})
    workloads = build_series(collect(results.get("benchmarks", [])))
    if not workloads:
        sys.exit(f"{arguments.results}: no benchmark repetitions found "
                 "(run with --benchmark_report_aggregates_only=false)")

    def order(key):
        container = key.split("/", 1)[0]
        rank = CONTAINER_ORDER.index(container) if container in CONTAINER_ORDER else len(CONTAINER_ORDER)
        return rank, container
    # Stable: within a container, workloads keep the order in which they ran.
    first_seen = list(dict.fromkeys(
        f"{p[0]}/{p[1]}" for p in (parse_name(b.get("run_name", b["name"])) for b in results["benchmarks"]) if p))
    ordered_keys = sorted((k for k in first_seen if k in workloads), key=order)

    metadata_path = arguments.metadata or arguments.results.with_name("metadata.json")
    metadata = Metadata(json.loads(metadata_path.read_text(encoding="utf-8")) if metadata_path.exists() else {})
    implementations = implementation_order({i for series in workloads.values() for i in series})
    tokens = color_tokens(implementations)
    body = render_body(context, workloads, ordered_keys, metadata, implementations, tokens, arguments.elapsed)

    chart_data = {
        "labels": {i: label(i) for i in implementations},
        "colorTokens": tokens,
        "baseline": BASELINE,
        "workloads": [
            {"key": key,
             "sizes": sorted({p["n"] for points in workloads[key].values() for p in points}),
             "implementations": [i for i in implementations if i in workloads[key]],
             "series": workloads[key]}
            for key in ordered_keys
        ],
    }
    page = render_page("template.html", body, chart_data, arguments.fragment)
    arguments.output.write_text(page, encoding="utf-8")
    print(f"wrote {arguments.output} ({len(ordered_keys)} workloads)")


def render_page(template_name, body, chart_data, fragment=False):
    """Fills a template of this folder with the body, the chart data and the shared style and script."""
    folder = Path(__file__).parent
    # "</" would end the <script> element early.
    data_json = json.dumps(chart_data, separators=(",", ":")).replace("</", "<\\/")
    page = (folder / template_name).read_text(encoding="utf-8")
    for placeholder, content in [("/*__REPORT_STYLE__*/", (folder / "style.css").read_text(encoding="utf-8")),
                                 ("/*__COMMON_SCRIPT__*/", (folder / "common.js").read_text(encoding="utf-8")),
                                 ("/*__REPORT_BODY__*/", body), ("/*__REPORT_DATA__*/", data_json)]:
        page = page.replace(placeholder, content)
    if not fragment:
        page = ('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
                '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
                f"{page}\n</html>\n")
    return page


if __name__ == "__main__":
    main()
