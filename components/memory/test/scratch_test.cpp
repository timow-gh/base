#include "base/memory/scratch.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <thread>

using base::memory::Arena;
using base::memory::ArenaConfig;
using base::memory::ScratchConfig;
using base::memory::ScratchArena;

namespace {

[[nodiscard]] const Arena* scratch_arena_of_new_scope() {
    const ScratchArena scratch;
    return &scratch.arena();
}

// Each thread has its own scratch arenas; a new thread starts without any.
template <typename Function>
void run_in_new_thread(Function function) {
    std::thread thread(function);
    thread.join();
}

} // namespace

TEST(ScratchArena, RewindsWhenDestroyed) {
    std::size_t usedBefore = 0;
    {
        const ScratchArena scratch;
        usedBefore = scratch.arena().used();
        ASSERT_NE(scratch.arena().allocate(1000, 8), nullptr);
        EXPECT_EQ(scratch.arena().used(), usedBefore + 1000U);
    }
    const ScratchArena again;
    EXPECT_EQ(again.arena().used(), usedBefore);
}

TEST(ScratchArena, WithoutConflictsTheSameArenaIsReused) {
    EXPECT_EQ(scratch_arena_of_new_scope(), scratch_arena_of_new_scope());
}

TEST(ScratchArena, AConflictSelectsTheOtherArena) {
    const ScratchArena first;
    const ScratchArena second({&first.arena()});
    EXPECT_NE(&second.arena(), &first.arena());

    const ScratchArena third({&second.arena()});
    EXPECT_EQ(&third.arena(), &first.arena());
}

TEST(ScratchArena, NestedOnTheSameArenaRewindInStackOrder) {
    const ScratchArena outer;
    ASSERT_NE(outer.arena().allocate(64, 8), nullptr);
    const std::size_t usedByOuter = outer.arena().used();
    {
        const ScratchArena inner;
        ASSERT_EQ(&inner.arena(), &outer.arena());
        ASSERT_NE(inner.arena().allocate(256, 8), nullptr);
    }
    EXPECT_EQ(outer.arena().used(), usedByOuter);
}

TEST(ScratchArena, AResultInTheCallersScratchArenaSurvivesTheCalleesScratchArena) {
    const ScratchArena caller;
    Arena& output = caller.arena();

    const auto produce = [](Arena& resultArena) {
        const ScratchArena scratch({&resultArena});
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

TEST(ScratchArena, EachThreadHasItsOwnArenas) {
    const Arena* mainArena = scratch_arena_of_new_scope();
    const Arena* threadArena = nullptr;
    std::thread worker([&threadArena] { threadArena = scratch_arena_of_new_scope(); });
    worker.join();
    EXPECT_NE(threadArena, nullptr);
    EXPECT_NE(threadArena, mainArena);
}

TEST(ScratchArenaDeathTest, ConflictingWithEveryScratchArenaAborts) {
    EXPECT_DEATH(
        {
            const ScratchArena first;
            const ScratchArena second({&first.arena()});
            const ScratchArena third({&first.arena(), &second.arena()});
        },
        "every scratch arena is in the conflicts");
}

TEST(ScratchArena, PrepareCreatesBothArenasWithTheConfigAndCommits) {
    run_in_new_thread([] {
        const ScratchConfig config{.arena = ArenaConfig{base::mib(256), base::kib(64)}, .initialCommit = base::mib(1)};
        ASSERT_TRUE(base::memory::prepare_scratch_arenas(config));

        const ScratchArena first;
        const ScratchArena second({&first.arena()});
        for (const Arena* arena : {&first.arena(), &second.arena()}) {
            EXPECT_EQ(arena->reserved(), base::mib(256));
            EXPECT_GE(arena->committed(), base::mib(1));
            EXPECT_EQ(arena->used(), 0U);
        }
    });
}

TEST(ScratchArena, PrepareAfterFirstUseOnlyCommits) {
    run_in_new_thread([] {
        const Arena* existing = scratch_arena_of_new_scope();
        const std::size_t reserved = existing->reserved();

        const ScratchConfig config{.arena = ArenaConfig{base::mib(256), base::kib(64)}, .initialCommit = base::mib(1)};
        ASSERT_TRUE(base::memory::prepare_scratch_arenas(config));
        EXPECT_EQ(existing->reserved(), reserved);
        EXPECT_GE(existing->committed(), base::mib(1));
    });
}

TEST(ScratchArena, PrepareReportsAFailedReservation) {
    run_in_new_thread([] {
        const ScratchConfig config{.arena = ArenaConfig{std::numeric_limits<std::size_t>::max() / 2U, base::kib(64)}};
        EXPECT_FALSE(base::memory::prepare_scratch_arenas(config));
    });
}

TEST(ScratchArena, PrepareWithPrefaultCommitsAndKeepsTheArenasEmpty) {
    run_in_new_thread([] {
        const ScratchConfig config{.initialCommit = base::mib(1), .prefault = base::memory::Prefault::Yes};
        ASSERT_TRUE(base::memory::prepare_scratch_arenas(config));

        const ScratchArena scratch;
        EXPECT_GE(scratch.arena().committed(), base::mib(1));
        EXPECT_EQ(scratch.arena().used(), 0U);
    });
}
