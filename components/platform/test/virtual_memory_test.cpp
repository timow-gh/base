#include "base/platform/virtual_memory.hpp"

#include "base/core/alignment.hpp"
#include "base/core/types.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <span>

namespace vm = base::platform;
using namespace base::types;

namespace {

[[nodiscard]] std::span<std::byte> as_bytes(void* address, usize size) {
    return {static_cast<std::byte*>(address), size};
}

} // namespace

TEST(VirtualMemory, PageSizeAndGranularityArePowersOfTwo) {
    EXPECT_TRUE(base::is_power_of_two(vm::page_size()));
    EXPECT_TRUE(base::is_power_of_two(vm::allocation_granularity()));
    EXPECT_GE(vm::allocation_granularity(), vm::page_size());
}

TEST(VirtualMemory, ReserveReturnsGranularityAlignedRange) {
    const usize size = vm::allocation_granularity() * 4U;
    void* address = vm::reserve(size);
    ASSERT_NE(address, nullptr);
    EXPECT_TRUE(base::is_aligned(reinterpret_cast<uptr>(address), vm::allocation_granularity()));
    vm::release(address, size);
}

TEST(VirtualMemory, CommittedPagesAreZeroAndWritable) {
    const usize size = vm::allocation_granularity() * 4U;
    void* address = vm::reserve(size);
    ASSERT_NE(address, nullptr);

    const usize pageSize = vm::page_size();
    ASSERT_TRUE(vm::commit(address, pageSize * 2U));
    std::span<std::byte> bytes = as_bytes(address, pageSize * 2U);
    EXPECT_TRUE(std::ranges::all_of(bytes, [](std::byte value) { return value == std::byte{0}; }));

    std::ranges::fill(bytes, std::byte{0xAB});
    EXPECT_EQ(bytes.front(), std::byte{0xAB});
    EXPECT_EQ(bytes.back(), std::byte{0xAB});

    vm::release(address, size);
}

TEST(VirtualMemory, RecommitAfterDecommitReturnsZeroedPages) {
    const usize size = vm::allocation_granularity();
    void* address = vm::reserve(size);
    ASSERT_NE(address, nullptr);

    const usize pageSize = vm::page_size();
    ASSERT_TRUE(vm::commit(address, pageSize));
    std::ranges::fill(as_bytes(address, pageSize), std::byte{0xCD});

    vm::decommit(address, pageSize);
    ASSERT_TRUE(vm::commit(address, pageSize));
    std::span<std::byte> bytes = as_bytes(address, pageSize);
    EXPECT_TRUE(std::ranges::all_of(bytes, [](std::byte value) { return value == std::byte{0}; }));

    vm::release(address, size);
}

TEST(VirtualMemory, LargeReservationDoesNotCommitMemory) {
    // 64 GiB of address space is cheap; only committed pages cost memory.
    const usize size = usize{64} * 1024U * 1024U * 1024U;
    void* address = vm::reserve(size);
    ASSERT_NE(address, nullptr);
    ASSERT_TRUE(vm::commit(address, vm::page_size()));
    as_bytes(address, vm::page_size()).front() = std::byte{1};
    vm::release(address, size);
}
