#include "base/core/types.hpp"
#include "base/memory/arena_allocator.hpp"
#include "base/memory/arena_containers.hpp"
#include "base/memory/scratch.hpp"

#include "base/platform/virtual_memory.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <list>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

using base::memory::Arena;
using base::memory::ArenaAllocator;
using base::memory::ArenaMap;
using base::memory::ArenaSet;
using base::memory::ArenaString;
using base::memory::ArenaUnorderedMap;
using base::memory::ArenaUnorderedSet;
using base::memory::ArenaVector;
using base::memory::ScratchArena;
using namespace base::types;

namespace {

[[nodiscard]] Arena make_arena(usize reserveSize = base::mib(64)) {
    std::optional<Arena> arena = Arena::create(reserveSize);
    if (!arena) {
        throw std::runtime_error("Arena::create failed");
    }
    return std::move(*arena);
}

class DestructionCounter {
public:
    explicit DestructionCounter(int& destroyed) noexcept
        : m_destroyed(&destroyed) {}
    DestructionCounter(const DestructionCounter&) = default;
    DestructionCounter& operator=(const DestructionCounter&) = default;
    DestructionCounter(DestructionCounter&&) = default;
    DestructionCounter& operator=(DestructionCounter&&) = default;
    ~DestructionCounter() { ++*m_destroyed; }

private:
    int* m_destroyed;
};

} // namespace

TEST(ArenaAllocator, VectorGrowsInsideTheArena) {
    Arena arena = make_arena();
    constexpr int count = 10'000;
    ArenaVector<int> values(arena);
    for (int i = 0; i < count; ++i) {
        values.push_back(i);
    }

    EXPECT_GE(arena.used(), count * sizeof(int));
    for (int i = 0; i < count; ++i) {
        ASSERT_EQ(values[static_cast<usize>(i)], i);
    }
}

TEST(ArenaAllocator, OutgrownVectorBuffersStayBelowTwiceTheFinalCapacity) {
    Arena arena = make_arena();
    ArenaVector<int> values(arena);
    for (int i = 0; i < 100'000; ++i) {
        values.push_back(i);
    }
    // The final buffer plus the outgrown ones, and some room for debug bookkeeping.
    EXPECT_LE(arena.used(), (3U * values.capacity() * sizeof(int)) + base::kib(1));
}

TEST(ArenaAllocator, StringBeyondTheSmallBufferAllocatesFromTheArena) {
    Arena arena = make_arena();
    ArenaString text(arena);
    text.assign(1000, 'x');
    EXPECT_EQ(text.size(), 1000U);
    EXPECT_GE(arena.used(), 1000U);
}

TEST(ArenaAllocator, NodeContainersRebindTheAllocator) {
    Arena arena = make_arena();
    ArenaMap<int, int> ordered(arena);
    ArenaSet<int> set(arena);
    ArenaUnorderedMap<int, int> hashed(arena);
    ArenaUnorderedSet<int> hashedSet(arena);
    std::list<int, ArenaAllocator<int>> list(arena);

    for (int i = 0; i < 1000; ++i) {
        ordered.emplace(i, i * 2);
        set.insert(i);
        hashed.emplace(i, i * 3);
        hashedSet.insert(i);
        list.push_back(i);
    }

    EXPECT_EQ(ordered.at(500), 1000);
    EXPECT_TRUE(set.contains(999));
    EXPECT_EQ(hashed.at(500), 1500);
    EXPECT_TRUE(hashedSet.contains(0));
    EXPECT_EQ(list.back(), 999);
    EXPECT_GE(arena.used(), 5U * 1000U * sizeof(int));
}

TEST(ArenaAllocator, AllocatorsAreEqualForTheSameArenaAcrossTypes) {
    Arena first = make_arena();
    Arena second = make_arena();
    const ArenaAllocator<int> ints(first);
    const ArenaAllocator<f64> doubles(ints);

    EXPECT_TRUE(ints == doubles);
    EXPECT_EQ(&doubles.arena(), &first);
    EXPECT_FALSE(ints == ArenaAllocator<int>(second));
}

TEST(ArenaAllocator, CopyConstructionUsesTheSameArena) {
    Arena arena = make_arena();
    ArenaVector<int> original(arena);
    original.assign({1, 2, 3});

    const ArenaVector<int> copy(original);
    EXPECT_EQ(&copy.get_allocator().arena(), &arena);
    EXPECT_EQ(copy, original);
}

TEST(ArenaAllocator, MoveConstructionTakesTheSourcesArena) {
    Arena arena = make_arena();
    ArenaVector<int> source(arena);
    source.assign({1, 2, 3});
    const int* data = source.data();

    const ArenaVector<int> moved(std::move(source));
    EXPECT_EQ(&moved.get_allocator().arena(), &arena);
    EXPECT_EQ(moved.data(), data);
}

TEST(ArenaAllocator, AssignmentBetweenArenasKeepsTheDestinationsArena) {
    Arena sourceArena = make_arena();
    Arena targetArena = make_arena();
    ArenaVector<int> source(sourceArena);
    source.assign({1, 2, 3});
    ArenaVector<int> copied(targetArena);
    ArenaVector<int> moved(targetArena);

    copied = source;
    moved = std::move(source);

    EXPECT_EQ(&copied.get_allocator().arena(), &targetArena);
    EXPECT_EQ(&moved.get_allocator().arena(), &targetArena);
    EXPECT_EQ(copied, (ArenaVector<int>({1, 2, 3}, targetArena)));
    EXPECT_EQ(moved, copied);
    EXPECT_GE(targetArena.used(), 6U * sizeof(int));
}

TEST(ArenaAllocator, ExhaustedArenaThrowsBadAlloc) {
    const usize granularity = base::platform::allocation_granularity();
    Arena arena = make_arena(granularity);
    ArenaVector<int> values(arena);
    EXPECT_THROW(values.reserve(granularity), std::bad_alloc);
}

TEST(ArenaAllocator, OverflowingCountThrowsBadArrayNewLength) {
    Arena arena = make_arena();
    ArenaAllocator<u64> allocator(arena);
    EXPECT_THROW(static_cast<void>(allocator.allocate(std::numeric_limits<usize>::max())), std::bad_array_new_length);
    EXPECT_EQ(arena.used(), 0U);
}

TEST(ArenaAllocator, ContainersRunElementDestructors) {
    Arena arena = make_arena();
    int destroyed = 0;
    {
        ArenaVector<DestructionCounter> counters(arena);
        counters.reserve(3);
        for (int i = 0; i < 3; ++i) {
            counters.emplace_back(destroyed);
        }
    }
    EXPECT_EQ(destroyed, 3);
}

TEST(ArenaAllocator, NestedContainersShareTheArena) {
    Arena arena = make_arena();
    ArenaVector<ArenaString> lines(arena);
    lines.emplace_back(500, 'a', arena);
    lines.emplace_back(500, 'b', arena);

    EXPECT_EQ(&lines.back().get_allocator().arena(), &arena);
    EXPECT_GE(arena.used(), 1000U);
}

TEST(ArenaAllocator, ContainerInAScratchArenaIsFreedWithIt) {
    usize usedBefore = 0;
    const Arena* scratchArena = nullptr;
    {
        const ScratchArena scratch;
        scratchArena = &scratch.arena();
        usedBefore = scratch.arena().used();
        ArenaVector<int> values(scratch.arena());
        values.resize(10'000);
        EXPECT_GT(scratch.arena().used(), usedBefore);
    }
    const ScratchArena again;
    ASSERT_EQ(&again.arena(), scratchArena);
    EXPECT_EQ(again.arena().used(), usedBefore);
}
