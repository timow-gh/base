#include "base/memory/arena.hpp"

#include "base/core/asan.hpp"
#include "base/core/assert.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

using base::memory::Arena;
using base::memory::arenaFreedByte;
using base::memory::arenaUninitializedByte;

namespace {

struct RecordedFailures {
    std::vector<std::string> messages;
};

RecordedFailures& recorded() {
    static RecordedFailures failures;
    return failures;
}

void recording_handler(std::string_view /*expression*/,
                       std::string_view message,
                       const std::source_location& /*location*/) {
    recorded().messages.emplace_back(message);
}

[[nodiscard]] bool all_bytes_equal(const std::byte* first, std::size_t count, std::byte expected) {
    return std::all_of(first, first + count, [expected](std::byte value) { return value == expected; });
}

class ArenaDebugTest : public ::testing::Test {
protected:
    void SetUp() override {
        recorded() = {};
        m_previous = base::set_assert_handler(&recording_handler);
        std::optional<Arena> created = Arena::create({base::mib(16), base::kib(64)});
        ASSERT_TRUE(created.has_value());
        m_arena.emplace(std::move(*created));
    }

    void TearDown() override {
        m_arena.reset();
        base::set_assert_handler(m_previous);
    }

    [[nodiscard]] Arena& arena() { return *m_arena; }

private:
    base::AssertHandler m_previous = nullptr;
    std::optional<Arena> m_arena;
};

} // namespace

TEST(ArenaDetail, FindModifiedFreedByte) {
    std::vector<std::byte> bytes(64, arenaFreedByte);
    EXPECT_FALSE(base::memory::detail::find_modified_freed_byte(bytes).has_value());

    bytes[17] = std::byte{0};
    bytes[40] = std::byte{1};
    const auto modified = base::memory::detail::find_modified_freed_byte(bytes);
    ASSERT_TRUE(modified.has_value());
    EXPECT_EQ(*modified, 17U);

    EXPECT_FALSE(base::memory::detail::find_modified_freed_byte({}).has_value());
}

TEST_F(ArenaDebugTest, AllocateFillsUninitializedPattern) {
    if (!Arena::debug_checks_enabled()) {
        GTEST_SKIP() << "base was built without BASE_ARENA_DEBUG";
    }
    const auto* memory = static_cast<const std::byte*>(arena().allocate(256, 8));
    ASSERT_NE(memory, nullptr);
    EXPECT_TRUE(all_bytes_equal(memory, 256, arenaUninitializedByte));
}

TEST_F(ArenaDebugTest, RewindFillsFreedPattern) {
    if (!Arena::debug_checks_enabled()) {
        GTEST_SKIP() << "base was built without BASE_ARENA_DEBUG";
    }
    auto* memory = static_cast<std::byte*>(arena().allocate(256, 8));
    ASSERT_NE(memory, nullptr);
    std::memset(memory, 0x42, 256);

    arena().reset();
    // Freed arena memory is poisoned under ASan; make it readable for the check.
    BASE_ASAN_UNPOISON(memory, 256);
    EXPECT_TRUE(all_bytes_equal(memory, 256, arenaFreedByte));
    BASE_ASAN_POISON(memory, 256);
    EXPECT_TRUE(recorded().messages.empty());
}

TEST_F(ArenaDebugTest, ReleaseBuildsLeaveFreedMemoryUntouched) {
    if (Arena::debug_checks_enabled()) {
        GTEST_SKIP() << "base was built with BASE_ARENA_DEBUG";
    }
    auto* memory = static_cast<std::byte*>(arena().allocate(256, 8));
    ASSERT_NE(memory, nullptr);
    std::memset(memory, 0x42, 256);

    arena().reset();
    BASE_ASAN_UNPOISON(memory, 256);
    EXPECT_TRUE(all_bytes_equal(memory, 256, std::byte{0x42}));
    BASE_ASAN_POISON(memory, 256);
}

TEST_F(ArenaDebugTest, ReusingMemoryWrittenAfterResetIsReported) {
    if (!Arena::debug_checks_enabled()) {
        GTEST_SKIP() << "base was built without BASE_ARENA_DEBUG";
    }
    auto* memory = static_cast<std::byte*>(arena().allocate(128, 8));
    ASSERT_NE(memory, nullptr);
    arena().reset();

    // A dangling pointer writes into freed memory. (Under ASan this access itself
    // would be reported, so it is unpoisoned first to exercise the pattern check.)
    BASE_ASAN_UNPOISON(memory, 128);
    memory[24] = std::byte{0x01};

    void* reused = arena().allocate(128, 8);
    EXPECT_EQ(reused, memory);
    ASSERT_EQ(recorded().messages.size(), 1U);
    EXPECT_NE(recorded().messages.front().find("offset 24"), std::string::npos) << recorded().messages.front();
}

TEST_F(ArenaDebugTest, UntouchedFreedMemoryIsNotReported) {
    ASSERT_NE(arena().allocate(128, 8), nullptr);
    const Arena::Marker marker = arena().mark_position();
    ASSERT_NE(arena().allocate(4096, 64), nullptr);
    arena().rewind_to(marker);
    ASSERT_NE(arena().allocate(8192, 16), nullptr);
    arena().reset();
    ASSERT_NE(arena().allocate(10000, 8), nullptr);
    EXPECT_TRUE(recorded().messages.empty());
}

TEST_F(ArenaDebugTest, TryGrowFillsTheGrownPartWithTheUninitializedPattern) {
    if (!Arena::debug_checks_enabled()) {
        GTEST_SKIP() << "base was built without BASE_ARENA_DEBUG";
    }
    auto* memory = static_cast<std::byte*>(arena().allocate(64, 8));
    ASSERT_NE(memory, nullptr);
    std::memset(memory, 0x42, 64);

    ASSERT_TRUE(arena().try_grow(memory, 64, 256));
    EXPECT_TRUE(all_bytes_equal(memory, 64, std::byte{0x42}));
    EXPECT_TRUE(all_bytes_equal(memory + 64, 192, arenaUninitializedByte));
}

TEST_F(ArenaDebugTest, TryGrowOverMemoryWrittenAfterResetIsReported) {
    if (!Arena::debug_checks_enabled()) {
        GTEST_SKIP() << "base was built without BASE_ARENA_DEBUG";
    }
    auto* memory = static_cast<std::byte*>(arena().allocate(128, 8));
    ASSERT_NE(memory, nullptr);
    arena().reset();
    ASSERT_EQ(arena().allocate(64, 8), memory);

    // A dangling pointer writes into the freed part the block will grow into.
    BASE_ASAN_UNPOISON(memory + 64, 64);
    memory[100] = std::byte{0x01};

    ASSERT_TRUE(arena().try_grow(memory, 64, 128));
    ASSERT_EQ(recorded().messages.size(), 1U);
    EXPECT_NE(recorded().messages.front().find("offset 100"), std::string::npos) << recorded().messages.front();
}

#if BASE_HAS_ASAN
TEST(ArenaAsanDeathTest, ReadingFreedMemoryIsReportedByAsan) {
    std::optional<Arena> arena = Arena::create({base::mib(1), base::kib(64)});
    ASSERT_TRUE(arena.has_value());
    volatile auto* memory = static_cast<volatile std::byte*>(arena->allocate(64, 8));
    ASSERT_NE(memory, nullptr);
    arena->reset();
    EXPECT_DEATH(static_cast<void>(memory[8]), "use-after-poison");
}

TEST(ArenaAsanDeathTest, ReadingPastTheLastAllocationIsReportedByAsan) {
    std::optional<Arena> arena = Arena::create({base::mib(1), base::kib(64)});
    ASSERT_TRUE(arena.has_value());
    volatile auto* memory = static_cast<volatile std::byte*>(arena->allocate(64, 8));
    ASSERT_NE(memory, nullptr);
    EXPECT_DEATH(static_cast<void>(memory[64]), "use-after-poison");
}
#endif

TEST_F(ArenaDebugTest, RewindToAMarkerPastThePositionAsserts) {
#if BASE_ENABLE_ASSERTS
    ASSERT_NE(arena().allocate(64, 8), nullptr);
    const Arena::Marker later = arena().mark_position();
    arena().reset();

    arena().rewind_to(later);
    EXPECT_EQ(recorded().messages.size(), 1U);
    EXPECT_EQ(arena().used(), 0U);
#else
    GTEST_SKIP() << "assertions are disabled";
#endif
}
