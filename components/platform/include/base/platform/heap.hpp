#pragma once

#include "base/core/types.hpp"

namespace base::platform {

// Blocks aligned to at most this come from malloc() itself, larger alignments from the
// system's aligned allocation. The same value that new-expressions use to choose
// between the plain and the aligned operator new.
inline constexpr usize defaultHeapAlignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;

/**
 * \brief Allocates `size` bytes from the C runtime heap, aligned to `alignment`.
 *
 * The building block of an operator new replacement: unlike malloc(), it also serves
 * over-aligned blocks, and unlike std::aligned_alloc(), on every platform and for any
 * size. O(1) apart from the system allocator.
 *
 * \param alignment A power of two. Must be passed unchanged to heap_free() and
 * heap_usable_size(): on Windows over-aligned blocks come from a different allocator.
 * \return nullptr if the system is out of memory.
 */
[[nodiscard]] void* heap_allocate(usize size, usize alignment) noexcept;

/**
 * \brief Frees a block returned by heap_allocate(). nullptr is ignored.
 * \param alignment The alignment the block was allocated with.
 */
void heap_free(void* memory, usize alignment) noexcept;

/**
 * \brief The bytes the system actually reserved for a block returned by
 * heap_allocate(), at least the requested size.
 *
 * Lets an operator delete that is not told the size, or told only the requested one,
 * account for exactly what the matching operator new accounted for, without storing
 * the size in a header of each block. 0 for nullptr.
 *
 * \param alignment The alignment the block was allocated with.
 */
[[nodiscard]] usize heap_usable_size(const void* memory, usize alignment) noexcept;

} // namespace base::platform
