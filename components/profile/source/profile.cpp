#include "base/profile/profile.hpp"

#include "base/core/assert.hpp"
#include "base/core/types.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <print>
#include <span>
#include <string>
#include <system_error>

namespace base::profile {

namespace {

using SteadyClock = std::chrono::steady_clock;

// The session starts when it is constructed, which is on the first use of any zone
// (see register_anchor()), and reports when it is destroyed at program exit.
struct Session {
    Session() noexcept
        : startTicks(read_cpu_timer())
        , startTime(SteadyClock::now()) {}

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;
    ~Session();

    Anchor* firstAnchor = nullptr;
    // Set by report() and print_report(): a program that reports on its own does not
    // get a second report at exit.
    bool reported = false;
    u64 startTicks;
    SteadyClock::time_point startTime;
};

// Function-local, so anchors registered during static initialization in other
// translation units find it constructed. Because the first anchor constructs it,
// it is destroyed after every later static object, and the anchors, which are
// trivially destructible, can still be read when it reports at exit.
Session& session() noexcept {
    static Session instance;
    return instance;
}

// The report sorts on the stack; zones beyond this many are summarized in one line.
constexpr usize maxReportedZones = 512;
// Wider names are not truncated, they only shift their own row.
constexpr usize maxNameColumnWidth = 40;
constexpr f64 millisecondsPerSecond = 1000.0;
constexpr f64 bytesPerGigabyte = 1e9;
constexpr f64 hertzPerGigahertz = 1e9;
constexpr f64 percent = 100.0;

struct Timing {
    u64 totalTicks = 0;
    f64 ticksPerSecond = 0.0;
};

[[nodiscard]] f64 to_milliseconds(u64 ticks, const Timing& timing) noexcept {
    return timing.ticksPerSecond > 0.0 ? static_cast<f64>(ticks) / timing.ticksPerSecond * millisecondsPerSecond : 0.0;
}

[[nodiscard]] f64 share_of_total(u64 ticks, const Timing& timing) noexcept {
    return timing.totalTicks > 0U ? static_cast<f64>(ticks) / static_cast<f64>(timing.totalTicks) * percent : 0.0;
}

[[nodiscard]] f64 gigabytes_per_second(const Anchor& anchor, const Timing& timing) noexcept {
    const f64 seconds =
        timing.ticksPerSecond > 0.0 ? static_cast<f64>(anchor.inclusiveTicks) / timing.ticksPerSecond : 0.0;
    return seconds > 0.0 ? static_cast<f64>(anchor.processedBytes) / seconds / bytesPerGigabyte : 0.0;
}

[[nodiscard]] u64 exclusive_ticks_of(const Anchor* anchor) noexcept {
    return anchor->exclusiveTicks;
}

[[nodiscard]] u64 allocated_bytes_of(const Anchor* anchor) noexcept {
    return anchor->allocatedBytes;
}

// What makes a zone expensive: exclusive_ticks_of() or allocated_bytes_of().
using CostOf = u64 (*)(const Anchor*) noexcept;

// The zones of one kind with hits, at most maxReportedZones of them, keeping the most
// expensive.
struct ReportedZones {
    std::array<const Anchor*, maxReportedZones> anchors{};
    usize count = 0;
    usize omittedCount = 0;
};

[[nodiscard]] ReportedZones collect_zones(const Anchor* firstAnchor, ZoneKind kind, CostOf costOf) noexcept {
    ReportedZones zones;
    for (const Anchor* anchor = firstAnchor; anchor != nullptr; anchor = anchor->next) {
        if (anchor->hitCount == 0U || anchor->kind != kind) {
            continue;
        }
        if (zones.count < zones.anchors.size()) {
            zones.anchors[zones.count++] = anchor;
            continue;
        }
        ++zones.omittedCount;
        // A reference, not the iterator: std::array's iterator is a pointer in libstdc++
        // but a class in MSVC's debug builds, so neither `auto` nor `auto*` fits both.
        const Anchor*& cheapest = *std::ranges::min_element(zones.anchors, std::ranges::less{}, costOf);
        if (costOf(anchor) > costOf(cheapest)) {
            cheapest = anchor;
        }
    }
    std::ranges::sort(std::span(zones.anchors).first(zones.count), std::ranges::greater{}, costOf);
    return zones;
}

[[nodiscard]] usize name_column_width(const ReportedZones& zones) noexcept {
    usize width = std::string_view("zone").size();
    for (usize i = 0; i < zones.count; ++i) {
        width = std::max(width, std::min(std::string_view(zones.anchors[i]->name).size(), maxNameColumnWidth));
    }
    return width;
}

void print_time_zone(std::FILE* stream, const Anchor& anchor, usize nameWidth, const Timing& timing) {
    std::print(stream,
               "{:<{}}  {:>10}  {:>10.2f} ms {:>5.1f}%  {:>10.2f} ms {:>5.1f}%",
               anchor.name,
               nameWidth,
               anchor.hitCount,
               to_milliseconds(anchor.exclusiveTicks, timing),
               share_of_total(anchor.exclusiveTicks, timing),
               to_milliseconds(anchor.inclusiveTicks, timing),
               share_of_total(anchor.inclusiveTicks, timing));
    if (anchor.processedBytes > 0U) {
        std::print(stream, "  {:>8.2f} GB/s", gigabytes_per_second(anchor, timing));
    }
    std::println(stream, "");
}

void write_time_table(std::FILE* stream, const ReportedZones& zones, const Timing& timing) {
    const usize nameWidth = name_column_width(zones);
    std::println(stream,
                 "Total: {:.2f} ms (CPU timer {:.2f} GHz)",
                 to_milliseconds(timing.totalTicks, timing),
                 timing.ticksPerSecond / hertzPerGigahertz);
    std::println(stream,
                 "{:<{}}  {:>10}  {:>20}  {:>20}  {:>13}",
                 "zone",
                 nameWidth,
                 "hits",
                 "exclusive",
                 "inclusive",
                 "bandwidth");
    for (usize i = 0; i < zones.count; ++i) {
        print_time_zone(stream, *zones.anchors[i], nameWidth, timing);
    }
    if (zones.omittedCount > 0U) {
        std::println(stream, "{} more zones not shown", zones.omittedCount);
    }
}

struct AllocationTotals {
    u64 count = 0;
    u64 bytes = 0;
};

// Over every allocation zone, also those the table leaves out. The counts are
// exclusive, so the sums are the allocations made inside zones, each counted once.
[[nodiscard]] AllocationTotals allocation_totals(const Anchor* firstAnchor) noexcept {
    AllocationTotals totals;
    for (const Anchor* anchor = firstAnchor; anchor != nullptr; anchor = anchor->next) {
        if (anchor->kind == ZoneKind::allocations) {
            totals.count += anchor->allocationCount;
            totals.bytes += anchor->allocatedBytes;
        }
    }
    return totals;
}

void write_allocation_table(std::FILE* stream, const ReportedZones& zones, const AllocationTotals& totals) {
    const usize nameWidth = name_column_width(zones);
    std::println(stream, "Allocations in zones: {} ({} bytes)", totals.count, totals.bytes);
    std::println(stream, "{:<{}}  {:>10}  {:>10}  {:>14}", "zone", nameWidth, "hits", "allocs", "alloc bytes");
    for (usize i = 0; i < zones.count; ++i) {
        const Anchor& anchor = *zones.anchors[i];
        std::println(stream,
                     "{:<{}}  {:>10}  {:>10}  {:>14}",
                     anchor.name,
                     nameWidth,
                     anchor.hitCount,
                     anchor.allocationCount,
                     anchor.allocatedBytes);
    }
    if (zones.omittedCount > 0U) {
        std::println(stream, "{} more zones not shown", zones.omittedCount);
    }
}

[[nodiscard]] bool has_hits(const Anchor* firstAnchor) noexcept {
    for (const Anchor* anchor = firstAnchor; anchor != nullptr; anchor = anchor->next) {
        if (anchor->hitCount > 0U) {
            return true;
        }
    }
    return false;
}

void write_report(std::FILE* stream, const Session& current) {
    const u64 endTicks = read_cpu_timer();
    const SteadyClock::time_point endTime = SteadyClock::now();

    const f64 seconds = std::chrono::duration<f64>(endTime - current.startTime).count();
    Timing timing;
    timing.totalTicks = endTicks - current.startTicks;
    timing.ticksPerSecond = seconds > 0.0 ? static_cast<f64>(timing.totalTicks) / seconds : 0.0;

    const ReportedZones timeZones = collect_zones(current.firstAnchor, ZoneKind::time, exclusive_ticks_of);
    const ReportedZones allocationZones =
        collect_zones(current.firstAnchor, ZoneKind::allocations, allocated_bytes_of);
    // A build has zones of one kind only. An allocation build's report leaves the time
    // table out, so that it shows no times distorted by the tracking.
    if (timeZones.count > 0U || allocationZones.count == 0U) {
        write_time_table(stream, timeZones, timing);
    }
    if (allocationZones.count > 0U) {
        write_allocation_table(stream, allocationZones, allocation_totals(current.firstAnchor));
    }
}

// The C++ Core Guidelines checks expect gsl::owner<> for C streams; the unique_ptr
// below is their owner instead.
struct FileCloser {
    void operator()(std::FILE* file) const noexcept {
        std::fclose(file); // NOLINT(cppcoreguidelines-owning-memory)
    }
};

using FileHandle = std::unique_ptr<std::FILE, FileCloser>;

[[nodiscard]] FileHandle open_for_writing(const std::filesystem::path& path) noexcept {
#if BASE_OS_WINDOWS
    // The wide variant opens non-ASCII paths; fopen_s avoids MSVC's deprecation of fopen.
    std::FILE* file = nullptr;
    return FileHandle(_wfopen_s(&file, path.c_str(), L"w") == 0 ? file : nullptr);
#else
    return FileHandle(std::fopen(path.c_str(), "w")); // NOLINT(cppcoreguidelines-owning-memory)
#endif
}

// <directory>/<name>-<UTC time>.txt; the timestamp keeps the reports of several runs
// apart, and UTC needs no time zone database.
[[nodiscard]] std::filesystem::path report_file_path() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    std::filesystem::path path = std::filesystem::path(BASE_PROFILE_REPORT_DIRECTORY) /
                                 std::format("{}-{:%Y%m%dT%H%M%SZ}.txt", BASE_PROFILE_REPORT_NAME, now);
    // The CMake option stores forward slashes; print the path as the OS writes it.
    return path.make_preferred();
}

void write_configured_report(const Session& current) {
    if (std::string_view(BASE_PROFILE_REPORT_DIRECTORY).empty()) {
        write_report(stdout, current);
        return;
    }
    const std::filesystem::path path = report_file_path();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const FileHandle file = open_for_writing(path);
    if (file == nullptr) {
        std::println(stderr, "profile: cannot write {}, printing the report instead", path.string());
        write_report(stdout, current);
        return;
    }
    write_report(file.get(), current);
    std::println(stdout, "profile: report written to {}", path.string());
}

Session::~Session() {
    if (reported) {
        return;
    }
    try {
        // Checked before the hits: a zone counts its hit on exit, so a program that
        // exits inside its only zone has none.
        if (detail::currentAnchor != nullptr) {
            // The open zones never added their time, so their parents' exclusive times
            // would be wrong.
            std::println(stdout, "profile: no report, the program exited inside a zone");
            return;
        }
        if (!has_hits(firstAnchor)) {
            return;
        }
        write_configured_report(*this);
    } catch (...) { // NOLINT(bugprone-empty-catch)
        // A destructor must not throw, and a report that failed at exit must not turn
        // a normal exit into std::terminate().
    }
}

} // namespace

namespace detail {

void register_anchor(Anchor& anchor) noexcept {
    anchor.next = std::exchange(session().firstAnchor, &anchor);
}

} // namespace detail

void reset() noexcept {
    BASE_ASSERT_MSG(detail::currentAnchor == nullptr, "profile::reset() inside an open zone");
    Session& current = session();
    for (Anchor* anchor = current.firstAnchor; anchor != nullptr; anchor = anchor->next) {
        anchor->hitCount = 0;
        anchor->exclusiveTicks = 0;
        anchor->inclusiveTicks = 0;
        anchor->processedBytes = 0;
        anchor->allocationCount = 0;
        anchor->allocatedBytes = 0;
    }
    current.startTime = SteadyClock::now();
    current.startTicks = read_cpu_timer();
}

std::optional<ZoneStats> zone_stats(std::string_view name) noexcept {
    for (const Anchor* anchor = session().firstAnchor; anchor != nullptr; anchor = anchor->next) {
        if (name == anchor->name) {
            return ZoneStats{.name = anchor->name,
                             .kind = anchor->kind,
                             .hitCount = anchor->hitCount,
                             .exclusiveTicks = anchor->exclusiveTicks,
                             .inclusiveTicks = anchor->inclusiveTicks,
                             .processedBytes = anchor->processedBytes,
                             .allocationCount = anchor->allocationCount,
                             .allocatedBytes = anchor->allocatedBytes};
        }
    }
    return std::nullopt;
}

void report() {
    BASE_ASSERT_MSG(detail::currentAnchor == nullptr, "profile::report() inside an open zone");
    Session& current = session();
    current.reported = true;
    write_configured_report(current);
}

void print_report(std::FILE* stream) {
    BASE_ASSERT_MSG(detail::currentAnchor == nullptr, "profile::print_report() inside an open zone");
    Session& current = session();
    current.reported = true;
    write_report(stream, current);
}

} // namespace base::profile
