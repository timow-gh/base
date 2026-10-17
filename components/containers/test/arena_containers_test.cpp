#include "base/containers/arena_map.hpp"
#include "base/containers/arena_set.hpp"
#include "base/containers/arena_string.hpp"
#include "base/containers/arena_unordered_map.hpp"
#include "base/containers/arena_unordered_set.hpp"
#include "base/containers/arena_vector.hpp"

#include "base/core/types.hpp"
#include "base/core/units.hpp"
#include "base/memory/arena.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <utility>

using base::containers::ArenaMap;
using base::containers::ArenaSet;
using base::containers::ArenaString;
using base::containers::ArenaUnorderedMap;
using base::containers::ArenaUnorderedSet;
using base::containers::ArenaVector;
using base::memory::Arena;
using namespace base::types;

namespace {

[[nodiscard]] Arena make_arena(usize reserveSize = base::mib(64)) {
    std::optional<Arena> arena = Arena::create(reserveSize);
    if (!arena) {
        throw std::runtime_error("Arena::create failed");
    }
    return std::move(*arena);
}

} // namespace

TEST(ArenaContainers, VectorAllocatesFromTheArena) {
    Arena arena = make_arena();
    ArenaVector<int> values(arena);
    values.assign({1, 2, 3});

    EXPECT_EQ(&values.get_allocator().arena(), &arena);
    EXPECT_GE(arena.used(), 3U * sizeof(int));
}

TEST(ArenaContainers, StringBeyondTheSmallBufferAllocatesFromTheArena) {
    Arena arena = make_arena();
    ArenaString text(arena);
    text.assign(1000, 'x');
    EXPECT_EQ(text.size(), 1000U);
    EXPECT_GE(arena.used(), 1000U);
}

TEST(ArenaContainers, NodeContainersRebindTheAllocator) {
    Arena arena = make_arena();
    ArenaMap<int, int> ordered(arena);
    ArenaSet<int> set(arena);
    ArenaUnorderedMap<int, int> hashed(arena);
    ArenaUnorderedSet<int> hashedSet(arena);

    for (int i = 0; i < 1000; ++i) {
        ordered.emplace(i, i * 2);
        set.insert(i);
        hashed.emplace(i, i * 3);
        hashedSet.insert(i);
    }

    EXPECT_EQ(ordered.at(500), 1000);
    EXPECT_TRUE(set.contains(999));
    EXPECT_EQ(hashed.at(500), 1500);
    EXPECT_TRUE(hashedSet.contains(0));
    EXPECT_GE(arena.used(), 4U * 1000U * sizeof(int));
}

TEST(ArenaContainers, NestedContainersShareTheArena) {
    Arena arena = make_arena();
    ArenaVector<ArenaString> lines(arena);
    lines.emplace_back(500, 'a', arena);
    lines.emplace_back(500, 'b', arena);

    EXPECT_EQ(&lines.back().get_allocator().arena(), &arena);
    EXPECT_GE(arena.used(), 1000U);
}
