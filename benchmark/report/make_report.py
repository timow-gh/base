"""Turns the Google Benchmark JSON of base_container_benchmarks into one HTML report.

Usage: make_report.py results.json report.html [--fragment]

Uses the raw repetitions (run with --benchmark_report_aggregates_only=false) and
computes its own statistics, so it does not depend on which aggregates the run
printed. Standard library only. --fragment leaves out the <!doctype html>, <head> and
<body> wrapper, for hosts that add their own (e.g. a published artifact).
"""

import argparse
import html
import json
import math
import statistics
import sys
from datetime import datetime, timezone
from pathlib import Path

VARIANTS = {
    "std": ("std::allocator", "The default heap: operator new and delete."),
    "pmr": ("pmr monotonic",
            "std::pmr::monotonic_buffer_resource over a buffer that fits one iteration, released after each."),
    "arena": ("Arena (warm)",
              "One base::memory::Arena for the whole run, committed up front, rewound by an ArenaScope after "
              "each iteration."),
    "arena_cold": ("Arena (cold)",
                   "A new Arena per iteration: reserving, committing, first-touch page faults and the release "
                   "are timed too."),
}

GROUPS = ["Vectors and strings", "Ordered containers", "Hash containers", "Locality", "Churn"]

# key: (title, group, description, what one operation is)
WORKLOADS = {
    "vector_push_back": (
        "vector push_back", "Vectors and strings",
        "Appends n random u64 to an empty vector without reserve. The vector grows geometrically; the arena "
        "and pmr variants keep every outgrown buffer, which shows in the memory chart.",
        "one push_back"),
    "vector_push_back_reserved": (
        "vector push_back after reserve", "Vectors and strings",
        "The same appends after reserve(n): a single allocation, so the loop itself dominates.",
        "one push_back"),
    "vector_of_short_strings": (
        "vector of short strings", "Vectors and strings",
        "Copies n strings of 4 to 15 characters into a vector of strings that share the vector's allocator. "
        "They fit the small-string buffer, so only the vector allocates.",
        "one string appended"),
    "vector_of_long_strings": (
        "vector of long strings", "Vectors and strings",
        "The same with 32 to 128 characters: every string allocates, nested inside the vector.",
        "one string appended"),
    "string_append": (
        "string append", "Vectors and strings",
        "Builds an n-byte string from 16-byte appends, like a log line or generated text.",
        "one 16-byte append"),
    "map_insert_lookup": (
        "map insert + lookup", "Ordered containers",
        "Inserts n unique keys into a std::map in random order, then looks each one up in another random order. "
        "One allocation per element.",
        "one insert or one lookup"),
    "set_insert_contains": (
        "set insert + contains", "Ordered containers",
        "The same pattern on a std::set.",
        "one insert or one contains"),
    "unordered_map_insert_lookup": (
        "unordered_map insert + lookup", "Hash containers",
        "Inserts n random keys without reserve, then performs n lookups: 90 % hits with Zipf-distributed key "
        "popularity (s = 1), 10 % misses. Every rehash leaves the old bucket array behind in the arena.",
        "one insert or one lookup"),
    "unordered_map_insert_lookup_reserved": (
        "unordered_map after reserve", "Hash containers",
        "The same after reserve(n): no rehashing, so only the node allocations differ.",
        "one insert or one lookup"),
    "unordered_set_insert_contains": (
        "unordered_set insert + contains", "Hash containers",
        "Insert and lookup pattern of the unordered_map workload on a std::unordered_set.",
        "one insert or one contains"),
    "map_lookup_prebuilt": (
        "map lookup, prebuilt", "Locality",
        "Times only the lookups on a map built before the loop. For std::allocator the nodes were allocated "
        "between unrelated 16 to 256-byte heap blocks, as on a heap in use; arena and pmr nodes sit next to "
        "each other.",
        "one lookup"),
    "map_iterate_prebuilt": (
        "map iteration, prebuilt", "Locality",
        "A full in-order traversal of the prebuilt map: pure pointer chasing from node to node.",
        "one element visited"),
    "unordered_map_lookup_prebuilt": (
        "unordered_map lookup, prebuilt", "Locality",
        "Lookups (90 % Zipf hits) on an unordered_map built before the loop, nodes interleaved with heap noise "
        "for std::allocator.",
        "one lookup"),
    "map_churn": (
        "map churn", "Churn",
        "A map that holds n entries erases its oldest key and inserts a new one, 8n times. std::allocator "
        "reuses the freed nodes; the arena and pmr never do, so their memory grows with every operation.",
        "one erase plus one insert"),
    "unordered_map_churn": (
        "unordered_map churn", "Churn",
        "The sliding window of the map churn workload on an unordered_map.",
        "one erase plus one insert"),
}

# Two-sided 95 % quantiles of Student's t for 1 to 30 degrees of freedom.
T_95 = [12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228, 2.201, 2.179, 2.160, 2.145,
        2.131, 2.120, 2.110, 2.101, 2.093, 2.086, 2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048,
        2.045, 2.042]

NOISY_CV_PERCENT = 5.0
TIME_UNIT_NS = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}


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


def parse_name(run_name):
    parts = run_name.split("/")
    if parts and parts[-1] == "real_time":
        parts = parts[:-1]
    if len(parts) != 3:
        return None
    workload, variant, size = parts
    try:
        return workload, variant, int(size)
    except ValueError:
        return None


def collect(benchmarks):
    """Groups the raw repetitions into cells keyed by (workload, variant, n)."""
    cells = {}
    for entry in benchmarks:
        if entry.get("run_type") != "iteration" or entry.get("error_occurred"):
            continue
        parsed = parse_name(entry.get("run_name", entry["name"]))
        if parsed is None or entry.get("items_per_second", 0) <= 0:
            continue
        cell = cells.setdefault(parsed, {"nsPerOp": [], "nsPerIteration": [], "counters": entry})
        cell["nsPerOp"].append(1e9 / entry["items_per_second"])
        cell["nsPerIteration"].append(entry["real_time"] * TIME_UNIT_NS.get(entry.get("time_unit", "ns"), 1.0))
    return cells


def build_series(cells):
    workloads = {}
    for (workload, variant, size), cell in cells.items():
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
        workloads.setdefault(workload, {}).setdefault(variant, []).append(point)

    for series in workloads.values():
        for points in series.values():
            points.sort(key=lambda p: p["n"])
        baseline = {p["n"]: p for p in series.get("std", [])}
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


def geomean(values):
    values = [v for v in values if v and v > 0]
    if not values:
        return None
    return math.exp(statistics.fmean(math.log(v) for v in values))


def verdict(points):
    """Counts the sizes at which a variant is clearly faster or slower than std."""
    faster = sum(1 for p in points if p["speedup"] is not None and p["speedupLow"] > 1.0)
    slower = sum(1 for p in points if p["speedup"] is not None and p["speedupHigh"] < 1.0)
    return faster, slower, len(points)


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


def verdict_chip(faster, slower, total):
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


def swatch(variant):
    return f'<span class="swatch" style="background: var(--v-{variant.replace("_", "-")})"></span>'


def context_rows(context):
    def cache(entry):
        instances = context.get("num_cpus", 1) // max(entry.get("num_sharing", 1), 1)
        return f"L{entry['level']} {entry['type'].lower()} {format_bytes(entry['size'])} ×{instances}"

    caches = ", ".join(cache(entry) for entry in context.get("caches", []))
    rows = [
        ("Date", context.get("date", "–")),
        ("Host", context.get("host_name", "–")),
        ("CPU", f"{context.get('num_cpus', '–')} logical cores at {context.get('mhz_per_cpu', '–')} MHz"),
        ("Caches", caches or "–"),
        ("Compiler", context.get("compiler", "–")),
        ("Build type", context.get("base_build_type", "–")),
        ("Commit", context.get("base_commit", "–")),
        ("Arena debug checks", context.get("arena_debug_checks", "–")),
        ("Asserts / ASan", f"{context.get('asserts', '–')} / {context.get('asan', '–')}"),
        ("CPU frequency scaling", "on" if context.get("cpu_scaling_enabled") else "off"),
    ]
    return rows


def collect_warnings(context, workloads):
    warnings = []
    if str(context.get("base_build_type", "")).lower() not in ("release", "relwithdebinfo"):
        warnings.append(("Not an optimized build",
                         f"base was built as {context.get('base_build_type', 'unknown')}. "
                         "Use a *-release-benchmark preset."))
    if context.get("library_build_type") == "debug":
        warnings.append(("Google Benchmark is a debug build", "Its timing overhead is larger than in release."))
    if context.get("arena_debug_checks") == "on":
        warnings.append(("Arena debug checks are on",
                         "Every arena allocation is filled with a pattern, which slows the arena variants."))
    if context.get("asserts") == "on":
        warnings.append(("Assertions are on", "BASE_ASSERT checks run inside the measured code."))
    if context.get("asan") == "on":
        warnings.append(("AddressSanitizer is on", "Every allocator pays for poisoning and shadow memory."))
    if context.get("cpu_scaling_enabled"):
        warnings.append(("CPU frequency scaling is on",
                         "Times can drift with the clock speed. Random interleaving spreads the drift over all "
                         "variants, but the spread per cell grows."))
    cells = [p for series in workloads.values() for points in series.values() for p in points]
    noisy = [p for p in cells if p["cv"] > NOISY_CV_PERCENT]
    if noisy:
        warnings.append((f"{len(noisy)} of {len(cells)} measurements are noisy",
                         f"Their coefficient of variation is above {NOISY_CV_PERCENT:.0f} %; they are marked in "
                         "the tables. Close other programs and rerun for tighter numbers."))
    reps = min((p["reps"] for p in cells), default=0)
    if 0 < reps < 5:
        warnings.append(("Few repetitions",
                         f"Only {reps} repetitions per measurement; confidence intervals are wide. "
                         "The default run uses 10."))
    return warnings


def headline_tiles(workloads):
    def speedups(variant, groups, versus=None):
        values = []
        for key, series in workloads.items():
            if WORKLOADS.get(key, ("", ""))[1] not in groups or variant not in series:
                continue
            reference = {p["n"]: p for p in series.get(versus, [])} if versus else None
            for point in series[variant]:
                if versus is None:
                    values.append(point["speedup"])
                elif point["n"] in reference:
                    values.append(reference[point["n"]]["median"] / point["median"])
        return geomean(values)

    allocating = ["Vectors and strings", "Ordered containers", "Hash containers"]
    tiles = []

    def add(label, value, detail):
        tone = speedup_tone(value)
        tiles.append({"label": label, "value": format_speedup(value), "detail": detail, "tone": tone})

    add("Arena vs std::allocator", speedups("arena", allocating),
        "geometric mean speedup over all sizes of the vector, string, map and hash workloads")
    add("Arena vs pmr monotonic", speedups("arena", allocating, versus="pmr"),
        "the same workloads against the standard library's own arena")
    add("Arena vs std::allocator, locality", speedups("arena", ["Locality"]),
        "lookups and traversal on prebuilt containers: node placement only")

    cold = workloads.get("vector_push_back_reserved", {})
    if "arena" in cold and "arena_cold" in cold:
        warm_point, cold_point = cold["arena"][0], cold["arena_cold"][0]
        overhead = cold_point["nsPerIteration"] - warm_point["nsPerIteration"]
        tiles.append({"label": "Cost of a fresh arena", "value": format_ns(overhead), "tone": "tone-warn",
                      "detail": f"extra time per iteration of the cold arena at n = {cold_point['n']}: "
                                "reserve, commit, page faults and release"})

    churn = workloads.get("map_churn", {})
    if "arena" in churn and "std" in churn:
        arena_point, std_point = churn["arena"][-1], churn["std"][-1]
        if arena_point.get("peakBytes") and std_point.get("peakBytes"):
            ratio = arena_point["peakBytes"] / std_point["peakBytes"]
            tiles.append({"label": "Arena memory under churn", "value": f"{ratio:.1f}×", "tone": "tone-bad",
                          "detail": f"peak bytes vs std::allocator after 8n erase + insert on a map of "
                                    f"n = {format_count(arena_point['n'])}"})
    return tiles


def render_summary(workloads, ordered_keys):
    head = "".join(f'<th class="left"><span class="variant-name">{swatch(v)}{esc(VARIANTS[v][0])}</span></th>'
                   for v in ["pmr", "arena", "arena_cold"])
    rows = []
    for key in ordered_keys:
        series = workloads[key]
        title, group = WORKLOADS.get(key, (key, ""))[:2]
        cells = []
        for variant in ["pmr", "arena", "arena_cold"]:
            points = series.get(variant)
            if not points:
                cells.append('<td class="left">–</td>')
                continue
            mean = geomean(p["speedup"] for p in points)
            low = min(p["speedup"] for p in points)
            high = max(p["speedup"] for p in points)
            faster, slower, total = verdict(points)
            cells.append(
                f'<td class="left"><span class="num {speedup_tone(mean)}"><b>{format_speedup(mean)}</b></span> '
                f'<span class="num" style="color: var(--muted)">({low:.2f}–{high:.2f})</span><br>'
                f'{verdict_chip(faster, slower, total)}</td>')
        memory = "–"
        if series.get("arena") and series.get("std"):
            arena_peak, std_peak = series["arena"][-1].get("peakBytes"), series["std"][-1].get("peakBytes")
            if arena_peak and std_peak:
                memory = f"{arena_peak / std_peak:.2f}×"
        rows.append(f'<tr><td class="left"><a href="#{esc(key)}">{esc(title)}</a><br>'
                    f'<span style="color: var(--muted); font-size: 0.8rem">{esc(group)}</span></td>'
                    f'{"".join(cells)}<td class="num">{memory}</td></tr>')
    return (f'<div class="table-wrap"><table><thead><tr><th class="left">Workload</th>{head}'
            f'<th>Arena memory vs std at largest n</th></tr></thead><tbody>{"".join(rows)}</tbody></table></div>')


def render_workload_table(series, variants):
    rows = []
    for variant in variants:
        for p in series[variant]:
            noisy = ' class="noisy"' if p["cv"] > NOISY_CV_PERCENT else ""
            speedup = "–" if variant == "std" or p["speedup"] is None else (
                f'<span class="{speedup_tone(p["speedup"])}">{format_speedup(p["speedup"])}</span> '
                f'<span style="color: var(--muted)">({p["speedupLow"]:.2f}–{p["speedupHigh"]:.2f})</span>')
            allocations = "–" if not p.get("allocations") else f'{int(p["allocations"]):,}'
            rows.append(
                f'<tr><td class="left"><span class="variant-name">{swatch(variant)}{esc(VARIANTS[variant][0])}'
                f'</span></td><td>{format_count(p["n"])}</td><td><b>{format_ns(p["median"])}</b></td>'
                f'<td>{format_ns(p["mean"])} ± {format_ns(p["mean"] - p["ciLow"])}</td>'
                f'<td{noisy}>{p["cv"]:.1f} %</td><td>{format_ns(p["min"])}</td><td>{format_ns(p["max"])}</td>'
                f'<td>{format_ns(p["p5"])} – {format_ns(p["p95"])}</td><td>{speedup}</td>'
                f'<td>{p["itemsPerSecond"] / 1e6:,.1f} M/s</td><td>{format_bytes(p.get("bytesPerElement"))}</td>'
                f'<td>{format_bytes(p.get("peakBytes"))}</td><td>{allocations}</td><td>{p["reps"]}</td></tr>')
    head = ("<th class=\"left\">Variant</th><th>n</th><th>Median</th><th>Mean ± 95% CI</th><th>CV</th><th>Min</th>"
            "<th>Max</th><th>p5 – p95</th><th>Speedup vs std</th><th>Throughput</th><th>Bytes / element</th>"
            "<th>Peak bytes</th><th>Allocations</th><th>Reps</th>")
    return f'<div class="table-wrap"><table><thead><tr>{head}</tr></thead><tbody>{"".join(rows)}</tbody></table></div>'


def render_body(context, workloads, ordered_keys, warnings, tiles):
    parts = []
    context_html = "".join(f"<div><dt>{esc(k)}</dt><dd>{esc(v)}</dd></div>" for k, v in context_rows(context))
    parts.append(
        '<header class="intro"><span class="eyebrow">base · containers component</span>'
        "<h1>Arena container benchmarks</h1>"
        "<p>The STL containers of <span class=\"mono\">base::containers</span> allocate from an "
        "<span class=\"mono\">Arena</span> through <span class=\"mono\">ArenaAllocator</span>. This report compares "
        "them with the same containers on the default heap and on the standard library's monotonic arena, on "
        "synthetic data that is identical for every variant. Lower times and higher speedups are better.</p>"
        f'<dl class="context">{context_html}</dl></header>')

    if warnings:
        items = "".join(f'<div class="warning"><strong>{esc(t)}.</strong> {esc(d)}</div>' for t, d in warnings)
        parts.append(f'<div class="warnings">{items}</div>')

    tile_html = "".join(
        f'<div class="tile"><span class="tile-label">{esc(t["label"])}</span>'
        f'<span class="tile-value {t["tone"]}">{esc(t["value"])}</span>'
        f'<span class="tile-detail">{esc(t["detail"])}</span></div>' for t in tiles)
    parts.append(f'<section class="tiles" aria-label="Headline figures">{tile_html}</section>')

    legend = "".join(
        f'<div class="legend-item">{swatch(v)}<b>{esc(label)}</b><span class="desc">{esc(desc)}</span></div>'
        for v, (label, desc) in VARIANTS.items())
    parts.append(
        '<section class="method" aria-labelledby="summary-title"><h2 id="summary-title">Verdict per workload</h2>'
        "<p>Geometric mean speedup over all sizes against std::allocator, the range across sizes in brackets, and "
        "at how many sizes the 95 % confidence intervals separate. Memory compares the peak bytes held at the "
        "largest size.</p>"
        f'<div class="legend">{legend}</div>{render_summary(workloads, ordered_keys)}</section>')

    jump = []
    for group in GROUPS:
        keys = [k for k in ordered_keys if WORKLOADS.get(k, ("", ""))[1] == group]
        if keys:
            links = " ".join(f'<a href="#{esc(k)}">{esc(WORKLOADS[k][0])}</a>' for k in keys)
            jump.append(f'<span class="group">{esc(group)}</span> {links}')
    parts.append(f'<nav class="jump" aria-label="Workloads">{"".join(f"<span>{j}</span>" for j in jump)}</nav>')

    for key in ordered_keys:
        series = workloads[key]
        title, group, description, operation = WORKLOADS.get(key, (key, "", "", "one operation"))
        variants = [v for v in VARIANTS if v in series]
        has_memory = any(p.get("bytesPerElement") for v in variants for p in series[v])
        has_churn = any(p["checkpoints"] for v in variants for p in series[v])
        charts = [("time", "Time per operation", f"Median and min–max band. One operation is {operation}."),
                  ("speedup", "Speedup vs std::allocator",
                   "Median ratio; the band spans the ratio of the 95 % confidence bounds. Above the dashed line "
                   "is faster.")]
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
            f'<figure class="chart" style="margin: 0"><h3>{esc(name)}</h3><div class="chart-box">'
            f'<canvas data-workload="{esc(key)}" data-chart="{kind}" role="img" '
            f'aria-label="{esc(name)} for {esc(title)}"></canvas></div>'
            f'<figcaption class="chart-note">{esc(note)}</figcaption></figure>'
            for kind, name, note in charts)
        parts.append(
            f'<section class="workload" id="{esc(key)}"><div class="workload-head">'
            f'<span class="eyebrow">{esc(group)}</span><h2>{esc(title)}</h2><p>{esc(description)}</p></div>'
            f'<div class="charts">{chart_html}</div>'
            f'<details><summary>All numbers for {esc(title)}</summary>{render_workload_table(series, variants)}'
            "</details></section>")

    parts.append(
        '<section class="method workload" id="method"><h2>How the numbers were made</h2><ul>'
        "<li>Each benchmark runs a workload in a timed loop; Google Benchmark picks the iteration count, and each "
        "measurement is repeated with the order of all benchmarks shuffled between repetitions.</li>"
        "<li>Time per operation is wall-clock time divided by the operations of one iteration. Every iteration "
        "builds the containers, uses them and destroys them, so construction and teardown are included.</li>"
        "<li>The statistics use the raw repetitions: median, mean with a 95 % confidence interval (Student's t), "
        f"standard deviation as coefficient of variation (CV, marked above {NOISY_CV_PERCENT:.0f} %), min, max and "
        "the 5th to 95th percentile.</li>"
        "<li>Memory comes from one untimed pass per benchmark: a counting allocator for std::allocator, a counting "
        "memory resource for pmr, and Arena::used() for the arena, which includes alignment padding.</li>"
        "<li>The inputs are generated from fixed seeds, without the standard library's distributions, so every "
        "variant, run and compiler sees the same keys and strings.</li>"
        "<li>Rerun with <span class=\"mono\">cmake --workflow --preset workflow-benchmark-msvc</span> (or -gcc, "
        "-clang) on an otherwise idle machine.</li></ul></section>")
    return "\n".join(parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("results", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--fragment", action="store_true",
                        help="write the page without <html>, <head> and <body>, for hosts that add them")
    arguments = parser.parse_args()

    results = json.loads(arguments.results.read_text(encoding="utf-8"))
    context = results.get("context", {})
    workloads = build_series(collect(results.get("benchmarks", [])))
    if not workloads:
        sys.exit(f"{arguments.results}: no benchmark repetitions found "
                 "(run with --benchmark_report_aggregates_only=false)")

    known = [k for k in WORKLOADS if k in workloads]
    ordered_keys = known + sorted(k for k in workloads if k not in WORKLOADS)
    for key in ordered_keys:
        if key not in WORKLOADS:
            WORKLOADS[key] = (key, "Other", "", "one operation")
    if "Other" not in GROUPS:
        GROUPS.append("Other")

    warnings = collect_warnings(context, workloads)
    tiles = headline_tiles(workloads)
    body = render_body(context, workloads, ordered_keys, warnings, tiles)

    chart_data = {
        "variantLabels": {k: v[0] for k, v in VARIANTS.items()},
        "workloads": [
            {"key": key,
             "sizes": sorted({p["n"] for points in workloads[key].values() for p in points}),
             "variants": [v for v in VARIANTS if v in workloads[key]],
             "series": workloads[key]}
            for key in ordered_keys
        ],
    }
    # "</" would end the <script> element early.
    data_json = json.dumps(chart_data, separators=(",", ":")).replace("</", "<\\/")

    template = (Path(__file__).parent / "template.html").read_text(encoding="utf-8")
    page = template.replace("/*__REPORT_BODY__*/", body).replace("/*__REPORT_DATA__*/", data_json)
    if not arguments.fragment:
        page = ('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
                '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
                f"{page}\n</html>\n")
    arguments.output.write_text(page, encoding="utf-8")
    generated = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    print(f"wrote {arguments.output} ({len(ordered_keys)} workloads, {generated})")


if __name__ == "__main__":
    main()
