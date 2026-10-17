#include "base/platform/heap.hpp"

#include "base/core/alignment.hpp"
#include "base/core/types.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstring>

namespace heap = base::platform;
using namespace base::types;

namespace {

[[nodiscard]] uptr address_of(const void* pointer) {
    return reinterpret_cast<uptr>(pointer);
}

} // namespace

TEST(Heap, PlainBlocksHoldAtLeastTheRequestedSize) {
    void* memory = heap::heap_allocate(100, heap::defaultHeapAlignment);
    ASSERT_NE(memory, nullptr);
    EXPECT_TRUE(base::is_aligned(address_of(memory), heap::defaultHeapAlignment));
    EXPECT_GE(heap::heap_usable_size(memory, heap::defaultHeapAlignment), 100U);
    std::memset(memory, 0xAB, 100);
    heap::heap_free(memory, heap::defaultHeapAlignment);
}

TEST(Heap, OverAlignedBlocksAreAligned) {
    constexpr std::array<usize, 4> alignments{heap::defaultHeapAlignment * 2U, 64U, 256U, 4096U};
    for (const usize alignment : alignments) {
        void* memory = heap::heap_allocate(100, alignment);
        ASSERT_NE(memory, nullptr) << alignment;
        EXPECT_TRUE(base::is_aligned(address_of(memory), alignment)) << alignment;
        EXPECT_GE(heap::heap_usable_size(memory, alignment), 100U) << alignment;
        std::memset(memory, 0xAB, 100);
        heap::heap_free(memory, alignment);
    }
}

TEST(Heap, NullptrIsIgnored) {
    heap::heap_free(nullptr, heap::defaultHeapAlignment);
    heap::heap_free(nullptr, 64);
    EXPECT_EQ(heap::heap_usable_size(nullptr, heap::defaultHeapAlignment), 0U);
    EXPECT_EQ(heap::heap_usable_size(nullptr, 64), 0U);
}
