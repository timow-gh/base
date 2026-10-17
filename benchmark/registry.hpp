#pragma once

#include "base/core/types.hpp"

#include <benchmark/benchmark.h>

#include <array>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace base::bench {

// Container kinds a workload is written against. An implementation offers a kind when
// its Containers type set provides the matching type, so an implementation of a single
// container, e.g. a flat map, runs exactly the workloads of that container.
struct VectorKind {
    static constexpr std::string_view name = "vector";
    template <typename Containers>
    static constexpr bool providedBy = requires { typename Containers::template Vector<u64>; };
};

struct StringKind {
    static constexpr std::string_view name = "string";
    template <typename Containers>
    static constexpr bool providedBy = requires { typename Containers::String; };
};

struct MapKind {
    static constexpr std::string_view name = "map";
    template <typename Containers>
    static constexpr bool providedBy = requires { typename Containers::template Map<u64, u64>; };
};

struct SetKind {
    static constexpr std::string_view name = "set";
    template <typename Containers>
    static constexpr bool providedBy = requires { typename Containers::template Set<u64>; };
};

struct UnorderedMapKind {
    static constexpr std::string_view name = "unordered_map";
    template <typename Containers>
    static constexpr bool providedBy = requires { typename Containers::template UnorderedMap<u64, u64>; };
};

struct UnorderedSetKind {
    static constexpr std::string_view name = "unordered_set";
    template <typename Containers>
    static constexpr bool providedBy = requires { typename Containers::template UnorderedSet<u64>; };
};

// The order in which --list, the runs and the report show the containers.
inline constexpr std::array<std::string_view, 6> containerOrder{
    VectorKind::name, StringKind::name, MapKind::name, SetKind::name, UnorderedMapKind::name, UnorderedSetKind::name,
};

inline constexpr i64 defaultMaxElements = i64{1} << 20;

/**
 * \brief One workload with one implementation: everything the command line filters on
 * and the report describes. Sizes are chosen later, by the suite.
 */
struct BenchmarkSpec {
    std::string_view container;
    std::string_view workload;
    std::string_view implementation;
    // Comma-separated, e.g. "allocation" or "locality".
    std::string_view tags;
    std::string_view description;
    // What one item of items_per_second is, e.g. "one push_back".
    std::string_view operation;
    std::string_view implementationDescription;
    i64 maxElements = defaultMaxElements;
    void (*function)(benchmark::State&) = nullptr;
};

template <typename... Implementations>
struct ImplementationList {};

/**
 * \brief The specs of `Workload` for every implementation whose type set provides the
 * workload's container kind; the others are skipped at compile time.
 *
 * A workload declares `using Container = MapKind;` (or another kind), `name`, `tags`,
 * `description`, `operation`, `maxElements` and `template <typename Implementation>
 * static void run(benchmark::State&)`.
 */
template <typename Workload, typename... Implementations>
[[nodiscard]] std::vector<BenchmarkSpec> specs_for(ImplementationList<Implementations...> /*implementations*/) {
    std::vector<BenchmarkSpec> specs;
    const auto add = [&specs]<typename Implementation>() {
        if constexpr (Workload::Container::template providedBy<typename Implementation::Containers>) {
            specs.push_back({.container = Workload::Container::name,
                             .workload = Workload::name,
                             .implementation = Implementation::name,
                             .tags = Workload::tags,
                             .description = Workload::description,
                             .operation = Workload::operation,
                             .implementationDescription = Implementation::description,
                             .maxElements = Workload::maxElements,
                             .function = &Workload::template run<Implementation>});
        }
    };
    (add.template operator()<Implementations>(), ...);
    return specs;
}

[[nodiscard]] inline std::vector<BenchmarkSpec> concat(std::initializer_list<std::vector<BenchmarkSpec>> lists) {
    std::vector<BenchmarkSpec> all;
    for (const std::vector<BenchmarkSpec>& list : lists) {
        all.insert(all.end(), list.begin(), list.end());
    }
    return all;
}

} // namespace base::bench
