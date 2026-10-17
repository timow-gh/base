#include "base/platform/heap.hpp"

#include "base/core/alignment.hpp"
#include "base/core/assert.hpp"
#include "base/core/types.hpp"

#include <cstdlib>
#include <malloc.h>

namespace base::platform {

// The CRT keeps over-aligned blocks apart: _aligned_malloc() blocks must be freed and
// measured with the _aligned_* functions, never with free() or _msize(). The alignment
// that every call receives selects the matching family.

void* heap_allocate(usize size, usize alignment) noexcept {
    BASE_ASSERT(is_power_of_two(alignment));
    if (alignment <= defaultHeapAlignment) {
        return std::malloc(size); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
    }
    return _aligned_malloc(size, alignment);
}

void heap_free(void* memory, usize alignment) noexcept {
    if (alignment <= defaultHeapAlignment) {
        std::free(memory); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
        return;
    }
    _aligned_free(memory);
}

usize heap_usable_size(const void* memory, usize alignment) noexcept {
    if (memory == nullptr) {
        return 0U;
    }
    // The CRT only reads the block, but its signatures take a non-const pointer.
    void* block = const_cast<void*>(memory); // NOLINT(cppcoreguidelines-pro-type-const-cast)
    if (alignment <= defaultHeapAlignment) {
        return _msize(block);
    }
    return _aligned_msize(block, alignment, 0);
}

} // namespace base::platform
