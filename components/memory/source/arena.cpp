#include "base/memory/arena.hpp"

#include "base/core/alignment.hpp"
#include "base/core/asan.hpp"
#include "base/core/assert.hpp"
#include "base/core/atomic.hpp"
#include "base/core/types.hpp"
#include "base/platform/virtual_memory.hpp"

#include <algorithm>
#include <array>
#include <atomic>
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

std::optional<usize> find_modified_freed_byte(std::span<const std::byte> bytes) noexcept {
    const auto modified = std::ranges::find_if(bytes, [](std::byte value) { return value != arenaFreedByte; });
    if (modified == bytes.end()) {
        return std::nullopt;
    }
    return static_cast<usize>(modified - bytes.begin());
}

} // namespace detail

namespace {

// Mutable globals, against Core Guidelines I.2: arenas of every thread report to them,
// including the scratch arenas that thread exits destroy after main() returned, so they
// must be constant-initialized and live for the whole program.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
constinit std::atomic<u64> totalReservedBytes{0};
constinit std::atomic<u64> totalCommittedBytes{0};
constinit std::atomic<u64> peakCommittedBytes{0};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

[[nodiscard]] usize round_up_to_multiple(usize value, usize multiple) noexcept {
    return ((value + multiple - 1U) / multiple) * multiple;
}

[[maybe_unused]] void report_use_after_free(usize offset) noexcept {
    // Formatted on the stack: the arena must not depend on another allocator.
    constexpr usize messageCapacity = 160;
    std::array<char, messageCapacity> buffer{};
    const auto result = std::format_to_n(buffer.data(),
                                         buffer.size(),
                                         "arena: memory at offset {} was written after reset/rewind (use-after-free)",
                                         offset);
    const auto length = static_cast<usize>(result.out - buffer.data());
    ::base::detail::assert_failed("freed arena memory is unmodified", std::string_view(buffer.data(), length));
}

} // namespace

ArenaTotals arena_totals() noexcept {
    return ArenaTotals{.reservedBytes = totalReservedBytes.load(std::memory_order_relaxed),
                       .committedBytes = totalCommittedBytes.load(std::memory_order_relaxed),
                       .peakCommittedBytes = peakCommittedBytes.load(std::memory_order_relaxed)};
}

std::optional<Arena> Arena::create(const ArenaConfig& config) noexcept {
    const usize granularity = platform::allocation_granularity();
    const usize pageSize = platform::page_size();
    if (config.reserveSize == 0U || config.reserveSize > std::numeric_limits<usize>::max() - granularity) {
        return std::nullopt;
    }

    const usize reserveSize = align_up(config.reserveSize, granularity);
    const usize commitStep = std::max(round_up_to_multiple(config.commitSize, pageSize), pageSize);
    void* base = platform::reserve(reserveSize);
    if (base == nullptr) {
        return std::nullopt;
    }
    totalReservedBytes.fetch_add(reserveSize, std::memory_order_relaxed);
    return Arena(static_cast<std::byte*>(base), reserveSize, commitStep);
}

std::optional<Arena> Arena::create(usize reserveSize) noexcept {
    return create(ArenaConfig{reserveSize, defaultArenaCommitSize});
}

Arena::Arena(std::byte* base, usize reserved, usize commitStep) noexcept
    : m_base(base)
    , m_reserved(reserved)
    , m_commitStep(commitStep) {}

Arena::Arena(Arena&& other) noexcept
    : m_base(std::exchange(other.m_base, nullptr))
    , m_reserved(std::exchange(other.m_reserved, 0U))
    , m_committed(std::exchange(other.m_committed, 0U))
    , m_commitStep(std::exchange(other.m_commitStep, 0U))
    , m_offset(std::exchange(other.m_offset, 0U))
    , m_highWater(std::exchange(other.m_highWater, 0U))
    , m_lastAllocation(std::exchange(other.m_lastAllocation, noLastAllocation))
    , m_allocationCount(std::exchange(other.m_allocationCount, 0U)) {}

Arena& Arena::operator=(Arena&& other) noexcept {
    if (this != &other) {
        release_reservation();
        m_base = std::exchange(other.m_base, nullptr);
        m_reserved = std::exchange(other.m_reserved, 0U);
        m_committed = std::exchange(other.m_committed, 0U);
        m_commitStep = std::exchange(other.m_commitStep, 0U);
        m_offset = std::exchange(other.m_offset, 0U);
        m_highWater = std::exchange(other.m_highWater, 0U);
        m_lastAllocation = std::exchange(other.m_lastAllocation, noLastAllocation);
        m_allocationCount = std::exchange(other.m_allocationCount, 0U);
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
    totalReservedBytes.fetch_sub(m_reserved, std::memory_order_relaxed);
    totalCommittedBytes.fetch_sub(m_committed, std::memory_order_relaxed);
    m_base = nullptr;
}

void* Arena::allocate(usize size, usize alignment) noexcept {
    return allocate_impl(size, alignment, Fill::Uninitialized);
}

void* Arena::allocate_zeroed(usize size, usize alignment) noexcept {
    return allocate_impl(size, alignment, Fill::Zeroed);
}

void* Arena::allocate_impl(usize size, usize alignment, Fill fill) noexcept {
    BASE_ASSERT(is_power_of_two(alignment));
    BASE_ASSERT_MSG(alignment <= platform::page_size(), "alignment larger than the page size is not supported");

    // The reservation is aligned to the allocation granularity, which is a multiple
    // of every supported alignment, so aligning the offset aligns the address, and
    // `begin` cannot pass m_reserved.
    const usize begin = align_up(m_offset, alignment);
    if (size > m_reserved - begin || !hand_out(begin, begin + size, fill)) {
        return nullptr;
    }
    m_lastAllocation = begin;
    ++m_allocationCount;
    return m_base + begin;
}

bool Arena::try_grow(void* memory, usize oldSize, usize newSize) noexcept {
    BASE_ASSERT(newSize >= oldSize);
    if (m_lastAllocation == noLastAllocation || memory != m_base + m_lastAllocation ||
        oldSize != m_offset - m_lastAllocation) {
        return false;
    }
    if (newSize > m_reserved - m_lastAllocation) {
        return false;
    }
    return hand_out(m_offset, m_lastAllocation + newSize, Fill::Uninitialized);
}

void* Arena::reallocate(void* memory, usize oldSize, usize newSize, usize alignment) noexcept {
    BASE_ASSERT(newSize >= oldSize);
    BASE_ASSERT(memory != nullptr || oldSize == 0U);
    if (memory != nullptr && try_grow(memory, oldSize, newSize)) {
        return memory;
    }
    void* moved = allocate(newSize, alignment);
    if (moved != nullptr && oldSize > 0U) {
        std::memcpy(moved, memory, oldSize);
    }
    return moved;
}

bool Arena::hand_out(usize begin, usize end, Fill fill) noexcept {
    if (end > m_committed && !commit_up_to(end)) {
        return false;
    }

    // [m_offset, m_highWater) is the freed region: in debug builds it holds
    // arenaFreedByte, under ASan it is poisoned. Padding in [m_offset, begin) stays
    // freed; only [begin, end) is handed out.
    const usize reusedEnd = std::min(end, m_highWater);
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
                end - begin);
#else
    // Bytes past the high water mark were never handed out and are still zero.
    if (fill == Fill::Zeroed && begin < reusedEnd) {
        std::memset(m_base + begin, 0, reusedEnd - begin);
    }
#endif

    BASE_ASAN_POISON(m_base + m_offset, begin - m_offset);

    m_offset = end;
    m_highWater = std::max(m_highWater, end);
    return true;
}

bool Arena::commit_up_to(usize end) noexcept {
    // m_reserved is a multiple of the page size, so the target stays page aligned.
    const usize target = std::min(round_up_to_multiple(end, m_commitStep), m_reserved);
    if (!platform::commit(m_base + m_committed, target - m_committed)) {
        return false;
    }
    BASE_ASAN_POISON(m_base + m_committed, target - m_committed);
    const usize added = target - m_committed;
    base::atomic_fetch_max(peakCommittedBytes, totalCommittedBytes.fetch_add(added, std::memory_order_relaxed) + added);
    m_committed = target;
    return true;
}

bool Arena::commit_at_least(usize size, Prefault prefault) noexcept {
    if (size > m_reserved) {
        return false;
    }
    if (size > m_committed && !commit_up_to(size)) {
        return false;
    }
    if (prefault == Prefault::Yes) {
        // Pages below the high water mark were handed out before, so they are mapped
        // already, and in debug builds their freed part must keep arenaFreedByte.
        const usize pageSize = platform::page_size();
        touch_pages(align_up(m_highWater, pageSize), std::min(align_up(size, pageSize), m_committed));
    }
    return true;
}

void Arena::touch_pages(usize begin, usize end) noexcept {
    if (begin >= end) {
        return;
    }
    // Writes zero because bytes above the high water mark must stay zero, which
    // allocate_zeroed() relies on. A read would not do: Linux maps a shared zero page
    // for it and faults again on the first write.
    BASE_ASAN_UNPOISON(m_base + begin, end - begin);
    const usize pageSize = platform::page_size();
    for (usize offset = begin; offset < end; offset += pageSize) {
        volatile std::byte* page = m_base + offset;
        *page = std::byte{0};
    }
    BASE_ASAN_POISON(m_base + begin, end - begin);
}

void Arena::rewind_to(Marker marker) noexcept {
    BASE_ASSERT_MSG(marker.offset <= m_offset, "rewind to a marker past the current position");
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
    m_lastAllocation = noLastAllocation;
}

bool Arena::debug_checks_enabled() noexcept {
    return BASE_ARENA_DEBUG != 0;
}

} // namespace base::memory
