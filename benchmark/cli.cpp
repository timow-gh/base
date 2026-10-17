#include "cli.hpp"

#include "base/core/config.hpp"
#include "base/memory/arena.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

namespace base::bench {

namespace {

using NameSet = std::set<std::string, std::less<>>;

[[nodiscard]] std::vector<std::string> split_list(std::string_view text) {
    std::vector<std::string> items;
    while (!text.empty()) {
        const usize comma = text.find(',');
        const std::string_view item = text.substr(0, comma);
        if (!item.empty()) {
            items.emplace_back(item);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        text.remove_prefix(comma + 1U);
    }
    return items;
}

template <typename Range>
[[nodiscard]] std::string join(const Range& values, std::string_view separator) {
    std::string joined;
    for (const auto& value : values) {
        if (!joined.empty()) {
            joined += separator;
        }
        joined += std::format("{}", value);
    }
    return joined;
}

[[nodiscard]] bool selects_all(const std::vector<std::string>& filter) {
    return filter.empty() || std::ranges::find(filter, "all") != filter.end();
}

[[nodiscard]] bool matches(const std::vector<std::string>& filter, std::string_view value) {
    return selects_all(filter) || std::ranges::find(filter, value) != filter.end();
}

// True if one of the comma-separated `tags` is in `filter`.
[[nodiscard]] bool matches_tags(const std::vector<std::string>& filter, std::string_view tags) {
    if (selects_all(filter)) {
        return true;
    }
    return std::ranges::any_of(split_list(tags),
                               [&filter](const std::string& tag) { return std::ranges::find(filter, tag) != filter.end(); });
}

template <typename Name>
[[nodiscard]] NameSet known_names(std::span<const BenchmarkSpec> specs, Name name) {
    NameSet names;
    for (const BenchmarkSpec& spec : specs) {
        for (const std::string& item : split_list(name(spec))) {
            names.insert(item);
        }
    }
    return names;
}

[[nodiscard]] std::optional<std::string> unknown_name(std::string_view flag, const std::vector<std::string>& names,
                                                      const NameSet& known) {
    for (const std::string& name : names) {
        if (name != "all" && !known.contains(name)) {
            return std::format("unknown {} '{}'; valid: {}, all", flag, name, join(known, ", "));
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::expected<std::vector<i64>, std::string> parse_sizes(std::string_view text) {
    std::vector<i64> sizes;
    for (const std::string& item : split_list(text)) {
        i64 size = 0;
        const char* const end = item.data() + item.size();
        const auto [parsedEnd, error] = std::from_chars(item.data(), end, size);
        if (error != std::errc{} || parsedEnd != end || size <= 0) {
            return std::unexpected(std::format("--size: '{}' is not a positive number of elements", item));
        }
        sizes.push_back(size);
    }
    return sizes;
}

[[nodiscard]] const Suite* find_suite(std::string_view name) {
    const auto found = std::ranges::find(suites, name, &Suite::name);
    return found == suites.end() ? nullptr : &*found;
}

// The value of `argument` if it is `--<name>=<value>`.
[[nodiscard]] std::optional<std::string_view> flag_value(std::string_view argument, std::string_view name) {
    if (argument.size() > name.size() && argument.starts_with(name) && argument[name.size()] == '=') {
        return argument.substr(name.size() + 1U);
    }
    return std::nullopt;
}

[[nodiscard]] bool has_argument(const std::vector<std::string>& arguments, std::string_view prefix) {
    return std::ranges::any_of(arguments, [prefix](const std::string& argument) { return argument.starts_with(prefix); });
}

[[nodiscard]] std::string compiler_name() {
#if BASE_COMPILER_CLANG
    return "clang " __clang_version__;
#elif BASE_COMPILER_MSVC
    return std::format("msvc {}", _MSC_FULL_VER);
#else
    return "gcc " __VERSION__;
#endif
}

[[nodiscard]] std::span<const i64> selected_sizes(const Options& options) {
    return options.sizes.empty() ? options.suite->sizes : std::span<const i64>(options.sizes);
}

} // namespace

std::expected<Options, std::string> parse_options(std::span<char* const> arguments,
                                                  std::span<const BenchmarkSpec> specs) {
    Options options;
    options.suite = find_suite(defaultSuite);
    if (arguments.empty()) {
        return options;
    }
    options.benchmarkArguments.emplace_back(arguments.front());
    for (const char* const raw : arguments.subspan(1)) {
        const std::string_view argument = raw;
        if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--list") {
            options.list = true;
        } else if (const auto suite = flag_value(argument, "--suite")) {
            options.suite = find_suite(*suite);
            if (options.suite == nullptr) {
                return std::unexpected(std::format("unknown suite '{}'; valid: {}", *suite,
                                                   join(suites | std::views::transform(&Suite::name), ", ")));
            }
        } else if (const auto containers = flag_value(argument, "--container")) {
            options.containers = split_list(*containers);
        } else if (const auto workloads = flag_value(argument, "--workload")) {
            options.workloads = split_list(*workloads);
        } else if (const auto implementations = flag_value(argument, "--impl")) {
            options.implementations = split_list(*implementations);
        } else if (const auto tags = flag_value(argument, "--tag")) {
            options.tags = split_list(*tags);
        } else if (const auto sizes = flag_value(argument, "--size")) {
            auto parsed = parse_sizes(*sizes);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            options.sizes = std::move(*parsed);
        } else {
            options.benchmarkArguments.emplace_back(argument);
        }
    }

    const std::pair<std::string_view, const std::vector<std::string>*> filters[] = {
        {"container", &options.containers},
        {"workload", &options.workloads},
        {"implementation", &options.implementations},
        {"tag", &options.tags},
    };
    const NameSet knownSets[] = {
        known_names(specs, [](const BenchmarkSpec& spec) { return spec.container; }),
        known_names(specs, [](const BenchmarkSpec& spec) { return spec.workload; }),
        known_names(specs, [](const BenchmarkSpec& spec) { return spec.implementation; }),
        known_names(specs, [](const BenchmarkSpec& spec) { return spec.tags; }),
    };
    for (usize i = 0; i < std::size(filters); ++i) {
        if (auto error = unknown_name(filters[i].first, *filters[i].second, knownSets[i])) {
            return std::unexpected(std::move(*error));
        }
    }
    return options;
}

std::vector<Selection> select(std::span<const BenchmarkSpec> specs, const Options& options) {
    const std::span<const i64> wanted = selected_sizes(options);
    std::vector<Selection> selection;
    for (const BenchmarkSpec& spec : specs) {
        if (!matches(options.containers, spec.container) || !matches(options.workloads, spec.workload) ||
            !matches(options.implementations, spec.implementation) || !matches_tags(options.tags, spec.tags)) {
            continue;
        }
        std::vector<i64> sizes;
        std::ranges::copy_if(wanted, std::back_inserter(sizes), [&spec](i64 size) { return size <= spec.maxElements; });
        if (!sizes.empty()) {
            selection.push_back({spec, std::move(sizes)});
        }
    }
    return selection;
}

std::string describe_filters(const Options& options) {
    std::vector<std::string> parts;
    const std::pair<std::string_view, const std::vector<std::string>*> filters[] = {
        {"container", &options.containers},
        {"workload", &options.workloads},
        {"impl", &options.implementations},
        {"tag", &options.tags},
    };
    for (const auto& [name, values] : filters) {
        if (!selects_all(*values)) {
            parts.push_back(std::format("{}={}", name, join(*values, ",")));
        }
    }
    if (!options.sizes.empty()) {
        parts.push_back(std::format("size={}", join(options.sizes, ",")));
    }
    return parts.empty() ? "none" : join(parts, " ");
}

std::vector<std::string> benchmark_arguments(const Options& options) {
    const Suite& suite = *options.suite;
    std::vector<std::string> arguments = options.benchmarkArguments;
    if (!has_argument(arguments, "--benchmark_repetitions")) {
        arguments.push_back(std::format("--benchmark_repetitions={}", suite.repetitions));
    }
    if (!has_argument(arguments, "--benchmark_min_time")) {
        arguments.push_back(std::format("--benchmark_min_time={}", suite.minTime));
    }
    if (!has_argument(arguments, "--benchmark_enable_random_interleaving")) {
        arguments.push_back(std::format("--benchmark_enable_random_interleaving={}", suite.interleave));
    }
    return arguments;
}

std::string help_text() {
    std::string text =
        "Container benchmarks: <container>/<workload>/<implementation>/<elements>.\n"
        "Usually started through benchmark/run.py, which also writes the report.\n\n"
        "  --suite=NAME          how long the run takes (default quick):\n";
    for (const Suite& suite : suites) {
        text += std::format("                          {:<7} {} repetition(s), sizes {}: {}\n", suite.name,
                            suite.repetitions, join(suite.sizes, " "), suite.purpose);
    }
    text +=
        "  --container=A,B       only these containers, e.g. map,vector\n"
        "  --workload=A,B        only these workloads, e.g. insert_lookup,churn\n"
        "  --impl=A,B            only these implementations, e.g. std,arena\n"
        "  --tag=A,B             only workloads with one of these tags: allocation, locality, churn\n"
        "  --size=N,M            these element counts instead of the suite's\n"
        "  --list                show what the flags select, then exit\n"
        "  --help                this text, then Google Benchmark's flags\n\n"
        "Filters take comma-separated names or 'all' and combine with AND; --list shows the valid names.\n"
        "Every other argument goes to Google Benchmark, e.g. --benchmark_repetitions=5 overrides the suite's.\n\n";
    return text;
}

std::string list_text(std::span<const Selection> selection, const Options& options) {
    struct Row {
        std::string_view container;
        std::string_view workload;
        std::string_view tags;
        std::vector<std::string_view> implementations;
        std::vector<i64> sizes;
    };
    std::vector<Row> rows;
    usize benchmarkCount = 0;
    for (const Selection& selected : selection) {
        const BenchmarkSpec& spec = selected.spec;
        auto row = std::ranges::find_if(rows, [&spec](const Row& candidate) {
            return candidate.container == spec.container && candidate.workload == spec.workload;
        });
        if (row == rows.end()) {
            rows.push_back({spec.container, spec.workload, spec.tags, {}, selected.sizes});
            row = std::prev(rows.end());
        }
        row->implementations.push_back(spec.implementation);
        benchmarkCount += selected.sizes.size();
    }

    const Suite& suite = *options.suite;
    std::string text = std::format("Suite {} ({}): {} repetition(s), min time {}\nFilters: {}\n\n", suite.name,
                                   suite.purpose, suite.repetitions, suite.minTime, describe_filters(options));
    text += std::format("{:<15} {:<24} {:<28} {:<12} {}\n", "container", "workload", "implementations", "tags", "sizes");
    for (const Row& row : rows) {
        text += std::format("{:<15} {:<24} {:<28} {:<12} {}\n", row.container, row.workload,
                            join(row.implementations, " "), row.tags, join(row.sizes, " "));
    }
    text += std::format("\n{} benchmarks; each runs {} time(s) unless --benchmark_repetitions says otherwise.\n",
                        benchmarkCount, suite.repetitions);
    return text;
}

void register_benchmarks(std::span<const Selection> selection) {
    for (const Selection& selected : selection) {
        const BenchmarkSpec& spec = selected.spec;
        auto* registered = benchmark::RegisterBenchmark(
            std::format("{}/{}/{}", spec.container, spec.workload, spec.implementation), spec.function);
        for (const i64 size : selected.sizes) {
            registered->Arg(size);
        }
        registered->Unit(benchmark::kMicrosecond)->UseRealTime();
    }
}

void add_context(std::span<const Selection> selection, const Options& options) {
    benchmark::AddCustomContext("base_build_type", BASE_BENCH_BUILD_TYPE);
    benchmark::AddCustomContext("base_commit", BASE_BENCH_GIT_COMMIT);
    benchmark::AddCustomContext("compiler", compiler_name());
    benchmark::AddCustomContext("arena_debug_checks", memory::Arena::debug_checks_enabled() ? "on" : "off");
    benchmark::AddCustomContext("asserts", BASE_ENABLE_ASSERTS ? "on" : "off");
    benchmark::AddCustomContext("asan", BASE_HAS_ASAN ? "on" : "off");
    benchmark::AddCustomContext("suite", std::string(options.suite->name));
    benchmark::AddCustomContext("filters", describe_filters(options));
    benchmark::AddCustomContext("sizes", join(selected_sizes(options), ","));

    NameSet described;
    for (const Selection& selected : selection) {
        const BenchmarkSpec& spec = selected.spec;
        const std::string workload = std::format("{}/{}", spec.container, spec.workload);
        if (described.insert(workload).second) {
            benchmark::AddCustomContext("workload_description/" + workload, std::string(spec.description));
            benchmark::AddCustomContext("workload_operation/" + workload, std::string(spec.operation));
            benchmark::AddCustomContext("workload_tags/" + workload, std::string(spec.tags));
        }
        const std::string implementation = "implementation/" + std::string(spec.implementation);
        if (described.insert(implementation).second) {
            benchmark::AddCustomContext(implementation, std::string(spec.implementationDescription));
        }
    }
}

} // namespace base::bench
