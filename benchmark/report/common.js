// Chart helpers shared by report.html and comparison.html; each page inlines this file.
window.reportCharts = (function () {
  "use strict";

  function token(name) {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  }
  function withAlpha(hex, alpha) {
    const value = parseInt(hex.slice(1), 16);
    return "rgba(" + ((value >> 16) & 255) + "," + ((value >> 8) & 255) + "," + (value & 255) + "," + alpha + ")";
  }
  function formatNs(ns) {
    if (ns >= 1e6) return (ns / 1e6).toPrecision(3) + " ms";
    if (ns >= 1e3) return (ns / 1e3).toPrecision(3) + " µs";
    return ns.toPrecision(3) + " ns";
  }
  function formatBytes(bytes) {
    const units = ["B", "KiB", "MiB", "GiB"];
    let unit = 0;
    while (bytes >= 1024 && unit < units.length - 1) { bytes /= 1024; unit += 1; }
    return bytes.toPrecision(3) + " " + units[unit];
  }
  function formatCount(count) {
    if (count >= 1048576 && count % 1048576 === 0) return count / 1048576 + "Mi";
    if (count >= 1024 && count % 1024 === 0) return count / 1024 + "Ki";
    return String(count);
  }

  function sizeAxis(sizes, title) {
    return {
      type: "logarithmic",
      title: { display: true, text: title, color: token("--muted") },
      ticks: { color: token("--muted"), callback: function (value) { return formatCount(value); } },
      afterBuildTicks: function (axis) { axis.ticks = sizes.map(function (size) { return { value: size }; }); },
      grid: { color: token("--grid") },
      border: { color: token("--line") }
    };
  }
  function valueAxis(type, title, format) {
    return {
      type: type,
      title: { display: true, text: title, color: token("--muted") },
      ticks: { color: token("--muted"), callback: function (value) { return format(value); }, maxTicksLimit: 7 },
      grid: { color: token("--grid") },
      border: { color: token("--line") }
    };
  }
  function baseOptions(scales, tooltipLabel) {
    return {
      responsive: true,
      maintainAspectRatio: false,
      animation: false,
      interaction: { mode: "nearest", intersect: false },
      scales: scales,
      plugins: {
        legend: {
          labels: {
            color: token("--ink"),
            boxWidth: 12,
            boxHeight: 12,
            filter: function (item) { return !item.text.startsWith("_"); }
          }
        },
        tooltip: {
          filter: function (item) { return !item.dataset.label.startsWith("_"); },
          callbacks: { label: tooltipLabel }
        }
      }
    };
  }
  // A shaded band between two series, drawn before the line it belongs to.
  function bandDatasets(points, lowKey, highKey, color, xKey) {
    const x = xKey || "n";
    return [
      { label: "_low", data: points.map(function (p) { return { x: p[x], y: p[lowKey] }; }),
        borderWidth: 0, pointRadius: 0, fill: false },
      { label: "_high", data: points.map(function (p) { return { x: p[x], y: p[highKey] }; }),
        borderWidth: 0, pointRadius: 0, fill: "-1", backgroundColor: withAlpha(color, 0.16) }
    ];
  }

  // Draws the charts now and again whenever the color scheme changes, since the colors
  // are read from the CSS tokens when a chart is made.
  function renderCharts(render) {
    const charts = [];
    function renderAll() {
      while (charts.length > 0) charts.pop().destroy();
      if (typeof Chart === "undefined") {
        document.querySelectorAll(".chart-box").forEach(function (box) {
          box.innerHTML = '<p class="chart-missing">The charts need Chart.js from cdnjs.cloudflare.com, which did not load. ' +
            "The tables hold every number.</p>";
        });
        return;
      }
      Chart.defaults.font.family = token("--font-body");
      Chart.defaults.color = token("--muted");
      render(function (chart) { charts.push(chart); });
    }
    renderAll();
    const scheme = window.matchMedia("(prefers-color-scheme: dark)");
    if (scheme.addEventListener) scheme.addEventListener("change", renderAll);
    new MutationObserver(renderAll).observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme"] });
  }

  return {
    token: token, withAlpha: withAlpha, formatNs: formatNs, formatBytes: formatBytes, formatCount: formatCount,
    sizeAxis: sizeAxis, valueAxis: valueAxis, baseOptions: baseOptions, bandDatasets: bandDatasets,
    renderCharts: renderCharts
  };
})();
