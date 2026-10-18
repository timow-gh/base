#pragma once

#include "base/core/config.hpp"
#include "base/core/types.hpp"
#include "base/profile/cpu_timer.hpp"

#include <cstdio>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

// Where the report goes. It is written when the program exits normally (returning
// from main() or calling std::exit(); not after std::abort() or a crash), or earlier
// by base::profile::report():
// - BASE_PROFILE_REPORT_DIRECTORY empty (the default): printed to stdout.
// - Otherwise: written to <BASE_PROFILE_REPORT_DIRECTORY>/<BASE_PROFILE_REPORT_NAME>-<UTC time>.txt,
//   e.g. profiles/game-20261016T134512Z.txt, and one line on stdout names the file.
//   The directory is created if it is missing; a relative one is relative to the
//   working directory of the program.
// Set them for the whole build with the CMake options base_PROFILING_REPORT_DIRECTORY
// and base_PROFILING_REPORT_NAME, or as compile definitions when building base.
// Defining them in your own source files has no effect: the compiled base library
// writes the report.
#ifndef BASE_PROFILE_REPORT_DIRECTORY
#define BASE_PROFILE_REPORT_DIRECTORY ""
#endif
#ifndef BASE_PROFILE_REPORT_NAME
#define BASE_PROFILE_REPORT_NAME "profile"
#endif

namespace base::profile {

struct Anchor;

namespace detail {

void register_anchor(Anchor& anchor) noexcept;

// The innermost open zone, which a new zone's time is subtracted from. A mutable
// global, against Core Guidelines I.2, because zones are entered on hot paths where a
// function call per zone would distort the measurement; profiling is single-threaded
// for the same reason.
inline Anchor* currentAnchor = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

/**
 * \brief The heap allocations recorded on one thread so far, see
 * base::profile::record_heap_allocation(). Allocation zones read it on entry and exit,
 * like time zones read the CPU timer.
 */
struct AllocationClock {
    u64 count = 0;
    u64 bytes = 0;
};

// Thread-local, so that allocations of other threads are never charged to the zones of
// the profiling thread. constinit, because operator new advances it, also before main()
// and after the report: it must never need dynamic initialization.
inline constinit thread_local AllocationClock allocationClock{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

} // namespace detail

/**
 * \brief What a zone measures. The zone macros create time zones, or allocation zones
 * in builds with base_PROFILING=ALLOCATIONS; a build never measures both, because
 * tracking allocations distorts the times.
 */
enum class ZoneKind : u8 {
    // Hits, exclusive and inclusive ticks and processed bytes, see Block.
    time,
    // Hits and the heap allocations made in the zone, see AllocationBlock.
    allocations,
};

/**
 * \brief The statistics of one profiling zone, collected since the session started
 * or since reset().
 *
 * Each zone macro creates one function-local static Anchor, which registers itself on
 * its first use. There is one anchor per macro expansion, not per name: a zone in a
 * function template has one anchor per instantiation.
 *
 * Exclusive ticks exclude the time spent in nested zones. Inclusive ticks include it,
 * but count a recursive zone only once, for its outermost call.
 *
 * Allocations are counted like exclusive ticks: those recorded with
 * record_heap_allocation() while the zone was the innermost open zone of its thread,
 * not those of nested zones.
 */
struct Anchor {
    /**
     * \param zoneName Must outlive the anchor, e.g. a string literal or `__func__`.
     * \param zoneKind Selects the block type that may time the zone and the report
     * table that lists it.
     */
    explicit Anchor(const char* zoneName, ZoneKind zoneKind = ZoneKind::time) noexcept
        : name(zoneName)
        , kind(zoneKind) {
        detail::register_anchor(*this);
    }

    Anchor(const Anchor&) = delete;
    Anchor& operator=(const Anchor&) = delete;
    Anchor(Anchor&&) = delete;
    Anchor& operator=(Anchor&&) = delete;
    ~Anchor() = default;

    const char* name;
    ZoneKind kind;
    u64 hitCount = 0;
    u64 exclusiveTicks = 0;
    u64 inclusiveTicks = 0;
    u64 processedBytes = 0;
    u64 allocationCount = 0;
    u64 allocatedBytes = 0;
    Anchor* next = nullptr;
};

// The report at program exit runs after the anchors' lifetime would end if they had a
// destructor; being trivially destructible, they are never destroyed and stay readable.
static_assert(std::is_trivially_destructible_v<Anchor>);

/**
 * \brief Times one pass through a zone, from its construction to its destruction.
 *
 * Use the BASE_PROFILE_* macros instead of creating blocks directly, so that zones
 * compile to nothing when profiling is disabled.
 *
 * The timer is read last on entry and first on exit, so the bookkeeping is not
 * counted as the zone's own time; what remains is about two timer reads per zone,
 * small enough to leave zones in the code, but too much for tight inner loops.
 */
class Block {
public:
    explicit Block(Anchor& anchor, u64 processedBytes = 0) noexcept
        : m_anchor(&anchor)
        , m_parent(std::exchange(detail::currentAnchor, &anchor))
        , m_oldInclusiveTicks(anchor.inclusiveTicks)
        , m_processedBytes(processedBytes)
        , m_startTicks(read_cpu_timer()) {}

    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    Block(Block&&) = delete;
    Block& operator=(Block&&) = delete;

    ~Block() {
        const u64 elapsedTicks = read_cpu_timer() - m_startTicks;
        ++m_anchor->hitCount;
        m_anchor->processedBytes += m_processedBytes;
        m_anchor->exclusiveTicks += elapsedTicks;
        // Overwritten, not added: an outer call of a recursive zone restores the value
        // from before its own entry, so the inner calls are not counted twice.
        m_anchor->inclusiveTicks = m_oldInclusiveTicks + elapsedTicks;
        if (m_parent != nullptr) {
            // May wrap around until the parent's own exit adds its elapsed time back;
            // unsigned arithmetic makes the final value exact.
            m_parent->exclusiveTicks -= elapsedTicks;
        }
        detail::currentAnchor = m_parent;
    }

private:
    Anchor* m_anchor;
    Anchor* m_parent;
    u64 m_oldInclusiveTicks;
    u64 m_processedBytes;
    u64 m_startTicks;
};

/**
 * \brief Counts the heap allocations of one pass through an allocation zone, from its
 * construction to its destruction, instead of timing it.
 *
 * The zone macros create it instead of Block in builds with base_PROFILING=ALLOCATIONS.
 * It reads no timer, only the calling thread's allocation clock, which
 * record_heap_allocation() advances.
 */
class AllocationBlock {
public:
    explicit AllocationBlock(Anchor& anchor) noexcept
        : m_anchor(&anchor)
        , m_parent(std::exchange(detail::currentAnchor, &anchor))
        , m_startAllocations(detail::allocationClock) {}

    AllocationBlock(const AllocationBlock&) = delete;
    AllocationBlock& operator=(const AllocationBlock&) = delete;
    AllocationBlock(AllocationBlock&&) = delete;
    AllocationBlock& operator=(AllocationBlock&&) = delete;

    ~AllocationBlock() {
        const u64 allocationCount = detail::allocationClock.count - m_startAllocations.count;
        const u64 allocatedBytes = detail::allocationClock.bytes - m_startAllocations.bytes;
        ++m_anchor->hitCount;
        m_anchor->allocationCount += allocationCount;
        m_anchor->allocatedBytes += allocatedBytes;
        if (m_parent != nullptr) {
            // Like exclusive ticks: may wrap around until the parent's own exit adds
            // these allocations back, and an inner call of a recursive zone takes from
            // the outer one what the outer one adds, so each allocation counts once.
            m_parent->allocationCount -= allocationCount;
            m_parent->allocatedBytes -= allocatedBytes;
        }
        detail::currentAnchor = m_parent;
    }

private:
    Anchor* m_anchor;
    Anchor* m_parent;
    detail::AllocationClock m_startAllocations;
};

/**
 * \brief A copy of one zone's statistics, see Anchor. A time zone leaves the
 * allocation fields 0, an allocation zone the ticks and processed bytes.
 */
struct ZoneStats {
    std::string_view name;
    ZoneKind kind = ZoneKind::time;
    u64 hitCount = 0;
    u64 exclusiveTicks = 0;
    u64 inclusiveTicks = 0;
    u64 processedBytes = 0;
    u64 allocationCount = 0;
    u64 allocatedBytes = 0;
};

/**
 * \brief Discards the statistics of every zone and restarts the session clock.
 *
 * Optional: the session starts by itself when the first zone runs. Call it to leave
 * something out of the report, e.g. after loading. Must not be called while a zone is
 * open. O(number of zones).
 */
void reset() noexcept;

/**
 * \brief Returns the statistics of the first zone named `name`, or std::nullopt if no
 * such zone has been entered since the program started. O(number of zones).
 */
[[nodiscard]] std::optional<ZoneStats> zone_stats(std::string_view name) noexcept;

/**
 * \brief Writes the report now, to the output that BASE_PROFILE_REPORT_DIRECTORY and
 * BASE_PROFILE_REPORT_NAME select, and turns the report at program exit off.
 *
 * Optional: without a call, the same report is written when the program exits
 * normally. The report lists the zones entered since the session started (or since
 * reset()), the most expensive (by exclusive time) first, with their share of the
 * total time and, for BASE_PROFILE_BANDWIDTH zones, the throughput. The timer
 * frequency is derived from the session's duration, so a longer session converts
 * ticks to milliseconds more precisely. Allocation zones are listed in a table of
 * their own instead, with hits and allocations, the most allocated bytes first. Must
 * not be called while a zone is open.
 */
void report();

/**
 * \brief Like report(), but writes to `stream` regardless of the configured output,
 * e.g. to a log file of the program's own. Also turns the report at exit off.
 */
void print_report(std::FILE* stream);

} // namespace base::profile

#define BASE_PROFILE_CONCAT_IMPL(left, right) left##right
#define BASE_PROFILE_CONCAT(left, right) BASE_PROFILE_CONCAT_IMPL(left, right)

#if BASE_ENABLE_PROFILING && BASE_PROFILE_ALLOCATIONS
// An allocation zone has no time to relate the bytes to, so the byte count is only
// type-checked, as in disabled builds. Parentheses for the same reason as below.
#define BASE_PROFILE_BANDWIDTH(name, bytes)                                                                            \
    static_cast<void>(sizeof(bytes));                                                                                  \
    static ::base::profile::Anchor BASE_PROFILE_CONCAT(baseProfileAnchor, __LINE__)(                                   \
        name, ::base::profile::ZoneKind::allocations);                                                                 \
    const ::base::profile::AllocationBlock BASE_PROFILE_CONCAT(baseProfileBlock, __LINE__) {                           \
        BASE_PROFILE_CONCAT(baseProfileAnchor, __LINE__)                                                               \
    }
#define BASE_PROFILE_SCOPE(name) BASE_PROFILE_BANDWIDTH(name, 0)
#define BASE_PROFILE_FUNCTION() BASE_PROFILE_SCOPE(__func__)
#elif BASE_ENABLE_PROFILING
/**
 * \brief Like BASE_PROFILE_SCOPE, and also adds `bytes` to the zone's processed bytes,
 * so the report shows its throughput.
 *
 * \internal Initialized with parentheses, not braces: a zone must also work inside a
 * macro argument such as EXPECT_EXIT's statement, and the preprocessor protects only
 * commas inside parentheses.
 */
#define BASE_PROFILE_BANDWIDTH(name, bytes)                                                                            \
    static ::base::profile::Anchor BASE_PROFILE_CONCAT(baseProfileAnchor, __LINE__){name};                             \
    const ::base::profile::Block BASE_PROFILE_CONCAT(baseProfileBlock, __LINE__)(                                      \
        BASE_PROFILE_CONCAT(baseProfileAnchor, __LINE__), static_cast<::base::u64>(bytes))

/**
 * \brief Times the rest of the enclosing scope as the zone `name`, which must outlive
 * the program's use of the zone (a string literal). In builds with
 * base_PROFILING=ALLOCATIONS it counts the scope's heap allocations instead.
 *
 * Nothing else is needed: the session starts with the first zone, and the report is
 * written at program exit to the output that BASE_PROFILE_REPORT_DIRECTORY selects.
 */
#define BASE_PROFILE_SCOPE(name) BASE_PROFILE_BANDWIDTH(name, 0)

/**
 * \brief Times the rest of the enclosing function as a zone named after it.
 */
#define BASE_PROFILE_FUNCTION() BASE_PROFILE_SCOPE(__func__)
#else
// The byte count is in sizeof, so it is type-checked but never evaluated, like the
// condition of a disabled BASE_ASSERT.
#define BASE_PROFILE_BANDWIDTH(name, bytes) static_cast<void>(sizeof(bytes))
#define BASE_PROFILE_SCOPE(name) static_cast<void>(0)
#define BASE_PROFILE_FUNCTION() static_cast<void>(0)
#endif
