#pragma once

#include "counting.hpp"
#include "registry.hpp"

#include "base/containers/arena_map.hpp"
#include "base/containers/arena_set.hpp"
#include "base/containers/arena_string.hpp"
#include "base/containers/arena_unordered_map.hpp"
#include "base/containers/arena_unordered_set.hpp"
#include "base/containers/arena_vector.hpp"
#include "base/core/types.hpp"
#include "base/memory/arena.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace base::bench {

/**
 * \brief What the untimed footprint pass of a workload measured.
 */
struct Footprint {
    // The most memory held at once: the live bytes for std::allocator, everything
    // allocated for the arena and pmr implementations, which free nothing before the end.
    usize peakBytes = 0;
    // All bytes allocated, freed or not; the arena counts its alignment padding too.
    usize allocatedBytes = 0;
    // Number of allocations; 0 for the arena, which does not count them.
    usize allocations = 0;
    // Bytes held at each checkpoint() of the workload.
    std::vector<usize> checkpoints;
};

inline constexpr auto noCheckpoint = [] {};

[[noreturn]] inline void fail(std::string_view message) noexcept {
    std::fprintf(stderr, "benchmark setup failed: %.*s\n", static_cast<int>(message.size()), message.data());
    std::abort();
}

[[nodiscard]] inline memory::Arena create_arena() noexcept {
    std::optional<memory::Arena> arena = memory::Arena::create();
    if (!arena) {
        fail("Arena::create");
    }
    return std::move(*arena);
}

struct StdContainers {
    template <typename T>
    using Vector = std::vector<T>;
    using String = std::string;
    template <typename Key, typename Value>
    using Map = std::map<Key, Value>;
    template <typename Key>
    using Set = std::set<Key>;
    template <typename Key, typename Value>
    using UnorderedMap = std::unordered_map<Key, Value>;
    template <typename Key>
    using UnorderedSet = std::unordered_set<Key>;
};

// StdContainers with CountingAllocator, for the footprint pass of StdImplementation.
struct CountingContainers {
    template <typename T>
    using Vector = std::vector<T, CountingAllocator<T>>;
    using String = std::basic_string<char, std::char_traits<char>, CountingAllocator<char>>;
    template <typename Key, typename Value>
    using Map = std::map<Key, Value, std::less<Key>, CountingAllocator<std::pair<const Key, Value>>>;
    template <typename Key>
    using Set = std::set<Key, std::less<Key>, CountingAllocator<Key>>;
    template <typename Key, typename Value>
    using UnorderedMap =
        std::unordered_map<Key, Value, std::hash<Key>, std::equal_to<Key>, CountingAllocator<std::pair<const Key, Value>>>;
    template <typename Key>
    using UnorderedSet = std::unordered_set<Key, std::hash<Key>, std::equal_to<Key>, CountingAllocator<Key>>;
};

// monotonic_buffer_resource is the standard library's arena
struct PmrContainers {
    template <typename T>
    using Vector = std::pmr::vector<T>;
    using String = std::pmr::string;
    template <typename Key, typename Value>
    using Map = std::pmr::map<Key, Value>;
    template <typename Key>
    using Set = std::pmr::set<Key>;
    template <typename Key, typename Value>
    using UnorderedMap = std::pmr::unordered_map<Key, Value>;
    template <typename Key>
    using UnorderedSet = std::pmr::unordered_set<Key>;
};

struct ArenaContainers {
    template <typename T>
    using Vector = containers::ArenaVector<T>;
    using String = containers::ArenaString;
    template <typename Key, typename Value>
    using Map = containers::ArenaMap<Key, Value>;
    template <typename Key>
    using Set = containers::ArenaSet<Key>;
    template <typename Key, typename Value>
    using UnorderedMap = containers::ArenaUnorderedMap<Key, Value>;
    template <typename Key>
    using UnorderedSet = containers::ArenaUnorderedSet<Key>;
};

namespace detail {

template <typename Work>
[[nodiscard]] Footprint measure_in_arena(Work& work) {
    memory::Arena arena = create_arena();
    Footprint footprint;
    work.template operator()<ArenaContainers>(arena, [&] { footprint.checkpoints.push_back(arena.used()); });
    footprint.peakBytes = arena.used();
    footprint.allocatedBytes = arena.used();
    return footprint;
}

} // namespace detail

struct StdImplementation {
    static constexpr std::string_view name = "std";
    static constexpr std::string_view description = "The default heap: operator new and delete.";
    using Containers = StdContainers;

    template <typename Work>
    [[nodiscard]] static Footprint measure(Work& work) {
        AllocationStats stats;
        Footprint footprint;
        work.template operator()<CountingContainers>(CountingAllocator<std::byte>(stats),
                                                     [&] { footprint.checkpoints.push_back(stats.liveBytes); });
        footprint.peakBytes = stats.peakBytes;
        footprint.allocatedBytes = stats.totalBytes;
        footprint.allocations = stats.allocations;
        return footprint;
    }

    template <typename Work>
    static void time(benchmark::State& state, Work& work, [[maybe_unused]] const Footprint& footprint) {
        for ([[maybe_unused]] auto iteration : state) {
            work.template operator()<StdContainers>(std::allocator<std::byte>(), noCheckpoint);
        }
    }

    template <typename Body>
    static void with_source(Body&& body) {
        body.template operator()<StdContainers>(std::allocator<std::byte>());
    }
};

struct PmrImplementation {
    static constexpr std::string_view name = "pmr";
    static constexpr std::string_view description =
        "std::pmr::monotonic_buffer_resource over a buffer that fits one iteration, released after each.";
    using Containers = PmrContainers;

    template <typename Work>
    [[nodiscard]] static Footprint measure(Work& work) {
        std::pmr::monotonic_buffer_resource monotonic(std::pmr::new_delete_resource());
        // Between the containers and the monotonic resource, so it counts what the
        // containers request, not the chunks the resource takes from upstream.
        CountingResource counting(monotonic);
        Footprint footprint;
        work.template operator()<PmrContainers>(static_cast<std::pmr::memory_resource*>(&counting),
                                                [&] { footprint.checkpoints.push_back(counting.stats().totalBytes); });
        footprint.peakBytes = counting.stats().totalBytes;
        footprint.allocatedBytes = counting.stats().totalBytes;
        footprint.allocations = counting.stats().allocations;
        return footprint;
    }

    template <typename Work>
    static void time(benchmark::State& state, Work& work, const Footprint& footprint) {
        // Large enough for one iteration including alignment padding, and zeroed, so its
        // pages are mapped: release() rewinds to it and no iteration reaches upstream.
        // This is the steady state, as for ArenaWarmImplementation. Shared by all calls and only
        // grown, so the zeroing is paid once rather than on every repetition.
        constexpr usize slack = 4096;
        static std::vector<std::byte> buffer;
        const usize needed = footprint.allocatedBytes + (footprint.allocations * alignof(std::max_align_t)) + slack;
        if (buffer.size() < needed) {
            buffer.resize(needed);
        }
        std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size(), std::pmr::new_delete_resource());
        for ([[maybe_unused]] auto iteration : state) {
            work.template operator()<PmrContainers>(static_cast<std::pmr::memory_resource*>(&resource), noCheckpoint);
            resource.release();
        }
    }

    template <typename Body>
    static void with_source(Body&& body) {
        std::pmr::monotonic_buffer_resource resource(std::pmr::new_delete_resource());
        body.template operator()<PmrContainers>(static_cast<std::pmr::memory_resource*>(&resource));
    }
};

struct ArenaWarmImplementation {
    static constexpr std::string_view name = "arena";
    static constexpr std::string_view description =
        "One base::memory::Arena for the whole run, committed up front, rewound by an ArenaScope after each "
        "iteration.";
    using Containers = ArenaContainers;

    template <typename Work>
    [[nodiscard]] static Footprint measure(Work& work) {
        return detail::measure_in_arena(work);
    }

    template <typename Work>
    static void time(benchmark::State& state, Work& work, const Footprint& footprint) {
        // Shared by all calls, so pages committed and mapped for one call stay mapped for
        // the next. Each iteration rewinds it, so it is empty between calls.
        static memory::Arena arena = create_arena();
        // Committed and mapped up front, like the warm heap of StdImplementation and the zeroed
        // buffer of PmrImplementation: this implementation measures the steady state.
        if (!arena.commit_at_least(footprint.allocatedBytes, memory::Prefault::Yes)) {
            fail("Arena::commit_at_least");
        }
        for ([[maybe_unused]] auto iteration : state) {
            const memory::ArenaScope scope(arena);
            work.template operator()<ArenaContainers>(arena, noCheckpoint);
        }
    }

    template <typename Body>
    static void with_source(Body&& body) {
        memory::Arena arena = create_arena();
        body.template operator()<ArenaContainers>(arena);
    }
};

struct ArenaColdImplementation {
    static constexpr std::string_view name = "arena_cold";
    static constexpr std::string_view description =
        "A new Arena per iteration: reserving, committing, first-touch page faults and the release are timed too.";
    using Containers = ArenaContainers;

    template <typename Work>
    [[nodiscard]] static Footprint measure(Work& work) {
        return detail::measure_in_arena(work);
    }

    template <typename Work>
    static void time(benchmark::State& state, Work& work, [[maybe_unused]] const Footprint& footprint) {
        for ([[maybe_unused]] auto iteration : state) {
            memory::Arena arena = create_arena();
            work.template operator()<ArenaContainers>(arena, noCheckpoint);
        }
    }
};

using AllImplementations =
    ImplementationList<StdImplementation, PmrImplementation, ArenaWarmImplementation, ArenaColdImplementation>;
// Without the cold arena: workloads on a container built before the timed loop would
// measure the same for both arena implementations.
using PrebuiltImplementations = ImplementationList<StdImplementation, PmrImplementation, ArenaWarmImplementation>;

} // namespace base::bench
