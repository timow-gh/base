#include "base/memory/scratch.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <thread>

using base::memory::Arena;
using base::memory::ScratchScope;

namespace {

[[nodiscard]] const Arena* scratch_arena_of_new_scope() {
    const ScratchScope scratch;
    return &scratch.arena();
}

} // namespace

TEST(ScratchScope, ScopeRewindsOnExit) {
    std::size_t usedBefore = 0;
    {
        const ScratchScope scratch;
        usedBefore = scratch.arena().used();
        ASSERT_NE(scratch.arena().allocate(1000, 8), nullptr);
        EXPECT_EQ(scratch.arena().used(), usedBefore + 1000U);
    }
    const ScratchScope again;
    EXPECT_EQ(again.arena().used(), usedBefore);
}

TEST(ScratchScope, WithoutConflictsTheSameArenaIsReused) {
    EXPECT_EQ(scratch_arena_of_new_scope(), scratch_arena_of_new_scope());
}

TEST(ScratchScope, AConflictSelectsTheOtherArena) {
    const ScratchScope first;
    const ScratchScope second({&first.arena()});
    EXPECT_NE(&second.arena(), &first.arena());

    const ScratchScope third({&second.arena()});
    EXPECT_EQ(&third.arena(), &first.arena());
}

TEST(ScratchScope, NestedScopesOnTheSameArenaRewindInStackOrder) {
    const ScratchScope outer;
    ASSERT_NE(outer.arena().allocate(64, 8), nullptr);
    const std::size_t usedByOuter = outer.arena().used();
    {
        const ScratchScope inner;
        ASSERT_EQ(&inner.arena(), &outer.arena());
        ASSERT_NE(inner.arena().allocate(256, 8), nullptr);
    }
    EXPECT_EQ(outer.arena().used(), usedByOuter);
}

TEST(ScratchScope, AResultInTheCallersScratchArenaSurvivesTheCalleesScope) {
    const ScratchScope caller;
    Arena& output = caller.arena();

    const auto produce = [](Arena& resultArena) {
        const ScratchScope scratch({&resultArena});
        EXPECT_NE(&scratch.arena(), &resultArena);
        EXPECT_NE(scratch.arena().allocate(4096, 8), nullptr);
        return resultArena.push<int>(42);
    };

    const std::size_t usedBefore = output.used();
    const int* result = produce(output);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, 42);
    EXPECT_EQ(output.used(), usedBefore + sizeof(int));
}

TEST(ScratchScope, EachThreadHasItsOwnArenas) {
    const Arena* mainArena = scratch_arena_of_new_scope();
    const Arena* threadArena = nullptr;
    std::thread worker([&threadArena] { threadArena = scratch_arena_of_new_scope(); });
    worker.join();
    EXPECT_NE(threadArena, nullptr);
    EXPECT_NE(threadArena, mainArena);
}

TEST(ScratchScopeDeathTest, ConflictingWithEveryScratchArenaAborts) {
    EXPECT_DEATH(
        {
            const ScratchScope first;
            const ScratchScope second({&first.arena()});
            const ScratchScope third({&first.arena(), &second.arena()});
        },
        "every scratch arena is in the conflicts");
}
