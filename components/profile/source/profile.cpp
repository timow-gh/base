#include "base/profile/profile.hpp"

#include "base/core/assert.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
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
    std::uint64_t startTicks;
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
constexpr std::size_t maxReportedZones = 512;
// Wider names are not truncated, they only shift their own row.
constexpr std::size_t maxNameColumnWidth = 40;
constexpr double millisecondsPerSecond = 1000.0;
constexpr double bytesPerGigabyte = 1e9;
constexpr double hertzPerGigahertz = 1e9;
constexpr double percent = 100.0;

struct Timing {
    std::uint64_t totalTicks = 0;
    double ticksPerSecond = 0.0;
};

[[nodiscard]] double to_milliseconds(std::uint64_t ticks, const Timing& timing) noexcept {
    return timing.ticksPerSecond > 0.0 ? static_cast<double>(ticks) / timing.ticksPerSecond * millisecondsPerSecond
                                       : 0.0;
}

[[nodiscard]] double share_of_total(std::uint64_t ticks, const Timing& timing) noexcept {
    return timing.totalTicks > 0U ? static_cast<double>(ticks) / static_cast<double>(timing.totalTicks) * percent
                                  : 0.0;
}

[[nodiscard]] double gigabytes_per_second(const Anchor& anchor, const Timing& timing) noexcept {
    const double seconds =
        timing.ticksPerSecond > 0.0 ? static_cast<double>(anchor.inclusiveTicks) / timing.ticksPerSecond : 0.0;
    return seconds > 0.0 ? static_cast<double>(anchor.processedBytes) / seconds / bytesPerGigabyte : 0.0;
}

[[nodiscard]] std::uint64_t exclusive_ticks_of(const Anchor* anchor) noexcept {
    return anchor->exclusiveTicks;
}

// The zones with hits, at most maxReportedZones of them, keeping the most expensive.
struct ReportedZones {
    std::array<const Anchor*, maxReportedZones> anchors{};
    std::size_t count = 0;
    std::size_t omittedCount = 0;
};

[[nodiscard]] ReportedZones collect_zones(const Anchor* firstAnchor) noexcept {
    ReportedZones zones;
    for (const Anchor* anchor = firstAnchor; anchor != nullptr; anchor = anchor->next) {
        if (anchor->hitCount == 0U) {
            continue;
        }
        if (zones.count < zones.anchors.size()) {
            zones.anchors[zones.count++] = anchor;
            continue;
        }
        ++zones.omittedCount;
        const auto cheapest = std::ranges::min_element(zones.anchors, std::ranges::less{}, exclusive_ticks_of);
        if (anchor->exclusiveTicks > (*cheapest)->exclusiveTicks) {
            *cheapest = anchor;
        }
    }
    std::ranges::sort(std::span(zones.anchors).first(zones.count), std::ranges::greater{}, exclusive_ticks_of);
    return zones;
}

[[nodiscard]] std::size_t name_column_width(const ReportedZones& zones) noexcept {
    std::size_t width = std::string_view("zone").size();
    for (std::size_t i = 0; i < zones.count; ++i) {
        width = std::max(width, std::min(std::string_view(zones.anchors[i]->name).size(), maxNameColumnWidth));
    }
    return width;
}

void print_zone(std::FILE* stream, const Anchor& anchor, std::size_t nameWidth, const Timing& timing) {
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

[[nodiscard]] bool has_hits(const Anchor* firstAnchor) noexcept {
    for (const Anchor* anchor = firstAnchor; anchor != nullptr; anchor = anchor->next) {
        if (anchor->hitCount > 0U) {
            return true;
        }
    }
    return false;
}

void write_report(std::FILE* stream, const Session& current) {
    const std::uint64_t endTicks = read_cpu_timer();
    const SteadyClock::time_point endTime = SteadyClock::now();

    const double seconds = std::chrono::duration<double>(endTime - current.startTime).count();
    Timing timing;
    timing.totalTicks = endTicks - current.startTicks;
    timing.ticksPerSecond = seconds > 0.0 ? static_cast<double>(timing.totalTicks) / seconds : 0.0;

    const ReportedZones zones = collect_zones(current.firstAnchor);
    const std::size_t nameWidth = name_column_width(zones);

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
    for (std::size_t i = 0; i < zones.count; ++i) {
        print_zone(stream, *zones.anchors[i], nameWidth, timing);
    }
    if (zones.omittedCount > 0U) {
        std::println(stream, "{} more zones not shown", zones.omittedCount);
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
    }
    current.startTime = SteadyClock::now();
    current.startTicks = read_cpu_timer();
}

std::optional<ZoneStats> zone_stats(std::string_view name) noexcept {
    for (const Anchor* anchor = session().firstAnchor; anchor != nullptr; anchor = anchor->next) {
        if (name == anchor->name) {
            return ZoneStats{.name = anchor->name,
                             .hitCount = anchor->hitCount,
                             .exclusiveTicks = anchor->exclusiveTicks,
                             .inclusiveTicks = anchor->inclusiveTicks,
                             .processedBytes = anchor->processedBytes};
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
