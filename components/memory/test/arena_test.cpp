#include "base/memory/arena.hpp"

#include "base/core/bits.hpp"
#include "base/platform/virtual_memory.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

using base::memory::Arena;
using base::memory::ArenaConfig;
using base::memory::ArenaScope;

namespace {

[[nodiscard]] Arena make_arena(const ArenaConfig& config = {base::mib(256), base::kib(64)}) {
    std::optional<Arena> arena = Arena::create(config);
    if (!arena) {
        throw std::runtime_error("Arena::create failed");
    }
    return std::move(*arena);
}

[[nodiscard]] std::uintptr_t address_of(const void* pointer) {
    return reinterpret_cast<std::uintptr_t>(pointer);
}

struct Point {
    int x = 0;
    int y = 0;
};

template <typename T>
concept Pushable = requires(Arena& arena) { arena.push<T>(); };

static_assert(Pushable<int>);
static_assert(Pushable<Point>);
static_assert(!Pushable<std::string>, "types with non-trivial destructors must be rejected");

} // namespace

TEST(Arena, CreateReservesWithoutCommitting) {
    const Arena arena = make_arena({base::mib(1), base::kib(64)});
    EXPECT_GE(arena.reserved(), base::mib(1));
    EXPECT_TRUE(base::is_aligned(arena.reserved(), base::platform::allocation_granularity()));
    EXPECT_EQ(arena.committed(), 0U);
    EXPECT_EQ(arena.used(), 0U);
}

TEST(Arena, CreateRejectsEmptyReservation) {
    EXPECT_FALSE(Arena::create({0, base::kib(64)}).has_value());
}

TEST(Arena, DefaultConfigReserves512MiB) {
    const Arena arena = make_arena(ArenaConfig{});
    EXPECT_EQ(arena.reserved(), base::mib(512));
    EXPECT_EQ(arena.committed(), 0U);
}

TEST(Arena, CreateFromSizeUsesTheUnits) {
    std::optional<Arena> arena = Arena::create(base::mib(1));
    ASSERT_TRUE(arena.has_value());
    EXPECT_GE(arena->reserved(), base::mib(1));
    EXPECT_LT(arena->reserved(), base::mib(1) + base::platform::allocation_granularity());
}

namespace {

struct alignas(16) Aligned16 {
    float values[3] = {}; // NOLINT(modernize-avoid-c-arrays): layout under test
};

static_assert(base::memory::arena_bytes_for<char>(10) == 10U);
static_assert(base::memory::arena_bytes_for<int>(10) == (10U * sizeof(int)) + alignof(int) - 1U);
static_assert(base::memory::arena_bytes_for<Aligned16>() == 16U + 15U);
static_assert(base::memory::arena_bytes_for<Aligned16>(0) == 0U);

} // namespace

TEST(Arena, ArenaBytesForHoldsThePushedArrayAfterAnOddAllocation) {
    // Fill a whole reservation exactly: the arena must hold the array even when the
    // previous allocation leaves the worst-case padding in front of it.
    const std::size_t granularity = base::platform::allocation_granularity();
    const std::size_t count = (granularity - 1U - (alignof(Aligned16) - 1U)) / sizeof(Aligned16);
    const std::size_t needed = 1U + base::memory::arena_bytes_for<Aligned16>(count);
    ASSERT_LE(needed, granularity);

    Arena arena = make_arena({needed, granularity});
    ASSERT_EQ(arena.reserved(), granularity);
    ASSERT_NE(arena.allocate(1, 1), nullptr);
    const std::span<Aligned16> values = arena.push_array<Aligned16>(count);
    EXPECT_EQ(values.size(), count);
}

TEST(Arena, SumOfArenaBytesForHoldsMixedPushes) {
    constexpr std::size_t vertexCount = 10'000;
    constexpr std::size_t indexCount = 30'000;
    constexpr std::size_t needed = base::memory::arena_bytes_for<Aligned16>(vertexCount) +
                                   base::memory::arena_bytes_for<std::uint32_t>(indexCount) +
                                   base::memory::arena_bytes_for<char>(3);

    Arena arena = make_arena({needed, base::kib(64)});
    EXPECT_EQ(arena.push_array<char>(3).size(), 3U);
    EXPECT_EQ(arena.push_array<Aligned16>(vertexCount).size(), vertexCount);
    EXPECT_EQ(arena.push_array<std::uint32_t>(indexCount).size(), indexCount);
    EXPECT_LE(arena.used(), needed);
}

TEST(Arena, EveryPowerOfTwoAlignmentUpToThePageSizeIsHonored) {
    Arena arena = make_arena();
    for (std::size_t alignment = 1; alignment <= base::platform::page_size(); alignment *= 2U) {
        // An odd-sized allocation first, so the next one actually needs padding.
        ASSERT_NE(arena.allocate(3, 1), nullptr);
        void* memory = arena.allocate(8, alignment);
        ASSERT_NE(memory, nullptr) << "alignment " << alignment;
        EXPECT_TRUE(base::is_aligned(address_of(memory), alignment)) << "alignment " << alignment;
    }
}

TEST(Arena, AllocationsAreLinearAndDoNotOverlap) {
    Arena arena = make_arena();
    auto* first = static_cast<std::byte*>(arena.allocate(16, 16));
    auto* second = static_cast<std::byte*>(arena.allocate(16, 16));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second, first + 16);
    EXPECT_EQ(arena.used(), 32U);

    std::memset(first, 0x11, 16);
    std::memset(second, 0x22, 16);
    EXPECT_EQ(first[15], std::byte{0x11});
    EXPECT_EQ(second[0], std::byte{0x22});
}

TEST(Arena, GrowsAcrossManyCommitSteps) {
    Arena arena = make_arena({base::mib(64), base::kib(64)});
    const std::size_t reservedBefore = arena.reserved();
    constexpr std::size_t chunkSize = base::kib(1);
    constexpr std::size_t chunkCount = 10U * 1024U;

    for (std::size_t i = 0; i < chunkCount; ++i) {
        auto* chunk = static_cast<std::byte*>(arena.allocate(chunkSize, 8));
        ASSERT_NE(chunk, nullptr);
        std::memset(chunk, static_cast<int>(i & 0xFFU), chunkSize);
    }

    EXPECT_EQ(arena.used(), chunkSize * chunkCount);
    EXPECT_GE(arena.committed(), base::mib(10));
    EXPECT_TRUE(base::is_aligned(arena.committed(), base::platform::page_size()));
    EXPECT_EQ(arena.reserved(), reservedBefore);
}

TEST(Arena, ExhaustedReservationReturnsNullptr) {
    const std::size_t granularity = base::platform::allocation_granularity();
    Arena arena = make_arena({granularity, granularity});
    EXPECT_EQ(arena.allocate(granularity + 1U, 1), nullptr);
    ASSERT_NE(arena.allocate(granularity, 1), nullptr);
    EXPECT_EQ(arena.allocate(1, 1), nullptr);
    EXPECT_EQ(arena.allocate(std::numeric_limits<std::size_t>::max(), 1), nullptr);
}

TEST(Arena, ResetRewindsToTheStartAndKeepsCommittedMemory) {
    Arena arena = make_arena();
    void* first = arena.allocate(base::kib(200), 16);
    ASSERT_NE(first, nullptr);
    const std::size_t committed = arena.committed();

    arena.reset();
    EXPECT_EQ(arena.used(), 0U);
    EXPECT_EQ(arena.committed(), committed);

    void* again = arena.allocate(base::kib(200), 16);
    EXPECT_EQ(again, first);
    EXPECT_EQ(arena.committed(), committed);
}

TEST(Arena, RestoreRewindsToTheSavedMarker) {
    Arena arena = make_arena();
    ASSERT_NE(arena.allocate(100, 8), nullptr);
    const Arena::Marker marker = arena.save();
    EXPECT_EQ(marker.offset, 100U);

    void* scratch = arena.allocate(500, 8);
    ASSERT_NE(scratch, nullptr);
    EXPECT_GT(arena.used(), 100U);

    arena.restore(marker);
    EXPECT_EQ(arena.used(), 100U);
    EXPECT_EQ(arena.allocate(500, 8), scratch);
}

TEST(Arena, NestedScopesRestoreTheirOwnPositions) {
    Arena arena = make_arena();
    ASSERT_NE(arena.allocate(64, 8), nullptr);
    {
        const ArenaScope outer(arena);
        ASSERT_NE(outer.arena().allocate(128, 8), nullptr);
        EXPECT_EQ(arena.used(), 192U);
        {
            const ArenaScope inner(arena);
            ASSERT_NE(arena.allocate(256, 8), nullptr);
            EXPECT_EQ(arena.used(), 448U);
        }
        EXPECT_EQ(arena.used(), 192U);
    }
    EXPECT_EQ(arena.used(), 64U);
}

TEST(Arena, PushConstructsFromArguments) {
    Arena arena = make_arena();
    Point* point = arena.push<Point>(3, 4);
    ASSERT_NE(point, nullptr);
    EXPECT_EQ(point->x, 3);
    EXPECT_EQ(point->y, 4);
    EXPECT_TRUE(base::is_aligned(address_of(point), alignof(Point)));

    const double* value = arena.push<double>(2.5);
    ASSERT_NE(value, nullptr);
    EXPECT_DOUBLE_EQ(*value, 2.5);
}

TEST(Arena, PushArrayValueInitializesReusedMemory) {
    Arena arena = make_arena();
    auto* dirty = static_cast<std::byte*>(arena.allocate(400, alignof(int)));
    ASSERT_NE(dirty, nullptr);
    std::memset(dirty, 0x7F, 400);
    arena.reset();

    const std::span<int> values = arena.push_array<int>(100);
    ASSERT_EQ(values.size(), 100U);
    EXPECT_TRUE(std::ranges::all_of(values, [](int value) { return value == 0; }));
}

TEST(Arena, PushArrayRejectsZeroAndOverflowingCounts) {
    Arena arena = make_arena();
    EXPECT_TRUE(arena.push_array<int>(0).empty());
    EXPECT_TRUE(arena.push_array<std::uint64_t>(std::numeric_limits<std::size_t>::max() / 4U).empty());
    EXPECT_EQ(arena.used(), 0U);
}

TEST(Arena, AllocateZeroedClearsReusedMemory) {
    Arena arena = make_arena();
    auto* dirty = static_cast<std::byte*>(arena.allocate(256, 8));
    ASSERT_NE(dirty, nullptr);
    std::memset(dirty, 0x5A, 256);
    arena.reset();

    // Larger than the dirty part: the tail comes from never-used, OS-zeroed memory.
    auto* zeroed = static_cast<std::byte*>(arena.allocate_zeroed(1024, 8));
    ASSERT_EQ(zeroed, dirty);
    EXPECT_TRUE(std::all_of(zeroed, zeroed + 1024, [](std::byte value) { return value == std::byte{0}; }));
}

TEST(Arena, MoveTransfersOwnership) {
    Arena source = make_arena();
    int* value = source.push<int>(42);
    ASSERT_NE(value, nullptr);

    Arena moved(std::move(source));
    EXPECT_EQ(source.reserved(), 0U); // NOLINT(bugprone-use-after-move): checking the moved-from state
    EXPECT_EQ(source.allocate(1, 1), nullptr);
    EXPECT_EQ(*value, 42);
    EXPECT_EQ(moved.used(), sizeof(int));

    Arena assigned = make_arena();
    assigned = std::move(moved);
    EXPECT_EQ(*value, 42);
    EXPECT_EQ(assigned.used(), sizeof(int));
    ASSERT_NE(assigned.push<int>(7), nullptr);
}

TEST(Arena, ZeroSizedAllocationReturnsAlignedPointer) {
    Arena arena = make_arena();
    void* memory = arena.allocate(0, 64);
    ASSERT_NE(memory, nullptr);
    EXPECT_TRUE(base::is_aligned(address_of(memory), std::size_t{64}));
}
