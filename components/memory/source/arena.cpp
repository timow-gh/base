#include "base/memory/arena.hpp"

#include "base/core/asan.hpp"
#include "base/core/assert.hpp"
#include "base/core/bits.hpp"
#include "base/platform/virtual_memory.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <string_view>

// Debug fill patterns and the use-after-free check. They follow the assertions by
// default; define BASE_ARENA_DEBUG to 0 or 1 when building base to override it.
#ifndef BASE_ARENA_DEBUG
#define BASE_ARENA_DEBUG BASE_ENABLE_ASSERTS
#endif

namespace base::memory {

namespace detail {

std::optional<std::size_t> find_modified_freed_byte(std::span<const std::byte> bytes) noexcept {
    const auto modified = std::ranges::find_if(bytes, [](std::byte value) { return value != arenaFreedByte; });
    if (modified == bytes.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(modified - bytes.begin());
}

} // namespace detail

namespace {

[[nodiscard]] std::size_t round_up_to_multiple(std::size_t value, std::size_t multiple) noexcept {
    return ((value + multiple - 1U) / multiple) * multiple;
}

[[maybe_unused]] void report_use_after_free(std::size_t offset) noexcept {
    // Formatted on the stack: the arena must not depend on another allocator.
    constexpr std::size_t messageCapacity = 160;
    std::array<char, messageCapacity> buffer{};
    const auto result = std::format_to_n(buffer.data(),
                                         buffer.size(),
                                         "arena: memory at offset {} was written after reset/restore (use-after-free)",
                                         offset);
    const auto length = static_cast<std::size_t>(result.out - buffer.data());
    ::base::detail::assert_failed("freed arena memory is unmodified", std::string_view(buffer.data(), length));
}

} // namespace

std::optional<Arena> Arena::create(const ArenaConfig& config) noexcept {
    const std::size_t granularity = platform::allocation_granularity();
    const std::size_t pageSize = platform::page_size();
    if (config.reserveSize == 0U || config.reserveSize > std::numeric_limits<std::size_t>::max() - granularity) {
        return std::nullopt;
    }

    const std::size_t reserveSize = align_up(config.reserveSize, granularity);
    const std::size_t commitStep = std::max(round_up_to_multiple(config.commitSize, pageSize), pageSize);
    void* base = platform::reserve(reserveSize);
    if (base == nullptr) {
        return std::nullopt;
    }
    return Arena(static_cast<std::byte*>(base), reserveSize, commitStep);
}

Arena::Arena(std::byte* base, std::size_t reserved, std::size_t commitStep) noexcept
    : m_base(base)
    , m_reserved(reserved)
    , m_commitStep(commitStep) {}

Arena::Arena(Arena&& other) noexcept
    : m_base(std::exchange(other.m_base, nullptr))
    , m_reserved(std::exchange(other.m_reserved, 0U))
    , m_committed(std::exchange(other.m_committed, 0U))
    , m_commitStep(std::exchange(other.m_commitStep, 0U))
    , m_offset(std::exchange(other.m_offset, 0U))
    , m_highWater(std::exchange(other.m_highWater, 0U)) {}

Arena& Arena::operator=(Arena&& other) noexcept {
    if (this != &other) {
        release_reservation();
        m_base = std::exchange(other.m_base, nullptr);
        m_reserved = std::exchange(other.m_reserved, 0U);
        m_committed = std::exchange(other.m_committed, 0U);
        m_commitStep = std::exchange(other.m_commitStep, 0U);
        m_offset = std::exchange(other.m_offset, 0U);
        m_highWater = std::exchange(other.m_highWater, 0U);
    }
    return *this;
}

Arena::~Arena() {
    release_reservation();
}

void Arena::release_reservation() noexcept {
    if (m_base == nullptr) {
        return;
    }
    // ASan keeps shadow memory across munmap/VirtualFree; a later mapping at the
    // same addresses would otherwise start out poisoned.
    BASE_ASAN_UNPOISON(m_base, m_committed);
    platform::release(m_base, m_reserved);
    m_base = nullptr;
}

void* Arena::allocate(std::size_t size, std::size_t alignment) noexcept {
    return allocate_impl(size, alignment, Fill::Uninitialized);
}

void* Arena::allocate_zeroed(std::size_t size, std::size_t alignment) noexcept {
    return allocate_impl(size, alignment, Fill::Zeroed);
}

void* Arena::allocate_impl(std::size_t size, std::size_t alignment, Fill fill) noexcept {
    BASE_ASSERT(is_power_of_two(alignment), "alignment must be a power of two");
    BASE_ASSERT(alignment <= platform::page_size(), "alignment larger than the page size is not supported");

    // The reservation is aligned to the allocation granularity, which is a multiple
    // of every supported alignment, so aligning the offset aligns the address, and
    // `begin` cannot pass m_reserved.
    const std::size_t begin = align_up(m_offset, alignment);
    if (size > m_reserved - begin) {
        return nullptr;
    }
    const std::size_t end = begin + size;
    if (end > m_committed && !commit_up_to(end)) {
        return nullptr;
    }

    // [m_offset, m_highWater) is the freed region: in debug builds it holds
    // arenaFreedByte, under ASan it is poisoned. Padding in [m_offset, begin) stays
    // freed; only [begin, end) is handed out.
    const std::size_t reusedEnd = std::min(end, m_highWater);
    BASE_ASAN_UNPOISON(m_base + m_offset, end - m_offset);

#if BASE_ARENA_DEBUG
    if (m_offset < reusedEnd) {
        const std::span<const std::byte> reused(m_base + m_offset, reusedEnd - m_offset);
        if (const auto modified = detail::find_modified_freed_byte(reused)) {
            report_use_after_free(m_offset + *modified);
        }
    }
    std::memset(m_base + begin,
                std::to_integer<int>(fill == Fill::Zeroed ? std::byte{0} : arenaUninitializedByte),
                size);
#else
    // Bytes past the high water mark were never handed out and are still zero.
    if (fill == Fill::Zeroed && begin < reusedEnd) {
        std::memset(m_base + begin, 0, reusedEnd - begin);
    }
#endif

    BASE_ASAN_POISON(m_base + m_offset, begin - m_offset);

    m_offset = end;
    m_highWater = std::max(m_highWater, end);
    return m_base + begin;
}

bool Arena::commit_up_to(std::size_t end) noexcept {
    // m_reserved is a multiple of the page size, so the target stays page aligned.
    const std::size_t target = std::min(round_up_to_multiple(end, m_commitStep), m_reserved);
    if (!platform::commit(m_base + m_committed, target - m_committed)) {
        return false;
    }
    BASE_ASAN_POISON(m_base + m_committed, target - m_committed);
    m_committed = target;
    return true;
}

void Arena::restore(Marker marker) noexcept {
    BASE_ASSERT(marker.offset <= m_offset, "restore to a marker past the current position");
    if (marker.offset >= m_offset) {
        return;
    }

#if BASE_ARENA_DEBUG
    // Padding inside the range is poisoned under ASan; unpoison before filling.
    BASE_ASAN_UNPOISON(m_base + marker.offset, m_offset - marker.offset);
    std::memset(m_base + marker.offset, std::to_integer<int>(arenaFreedByte), m_offset - marker.offset);
#endif
    BASE_ASAN_POISON(m_base + marker.offset, m_offset - marker.offset);
    m_offset = marker.offset;
}

bool Arena::debug_checks_enabled() noexcept {
    return BASE_ARENA_DEBUG != 0;
}

} // namespace base::memory
