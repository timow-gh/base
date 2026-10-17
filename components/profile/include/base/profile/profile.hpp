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

} // namespace detail

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
 */
struct Anchor {
    /**
     * \param zoneName Must outlive the anchor, e.g. a string literal or `__func__`.
     */
    explicit Anchor(const char* zoneName) noexcept
        : name(zoneName) {
        detail::register_anchor(*this);
    }

    Anchor(const Anchor&) = delete;
    Anchor& operator=(const Anchor&) = delete;
    Anchor(Anchor&&) = delete;
    Anchor& operator=(Anchor&&) = delete;
    ~Anchor() = default;

    const char* name;
    u64 hitCount = 0;
    u64 exclusiveTicks = 0;
    u64 inclusiveTicks = 0;
    u64 processedBytes = 0;
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
 * \brief A copy of one zone's statistics, see Anchor.
 */
struct ZoneStats {
    std::string_view name;
    u64 hitCount = 0;
    u64 exclusiveTicks = 0;
    u64 inclusiveTicks = 0;
    u64 processedBytes = 0;
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
 * ticks to milliseconds more precisely. Must not be called while a zone is open.
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

#if BASE_ENABLE_PROFILING
/**
 * \brief Like BASE_PROFILE_SCOPE, and also adds `bytes` to the zone's processed bytes,
 * so the report shows its throughput.
 */
#define BASE_PROFILE_BANDWIDTH(name, bytes)                                                                            \
    static ::base::profile::Anchor BASE_PROFILE_CONCAT(baseProfileAnchor, __LINE__){name};                             \
    const ::base::profile::Block BASE_PROFILE_CONCAT(baseProfileBlock, __LINE__) {                                     \
        BASE_PROFILE_CONCAT(baseProfileAnchor, __LINE__), static_cast<::base::u64>(bytes)                              \
    }

/**
 * \brief Times the rest of the enclosing scope as the zone `name`, which must outlive
 * the program's use of the zone (a string literal).
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
