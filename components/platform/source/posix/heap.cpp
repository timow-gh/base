#include "base/platform/heap.hpp"

#include "base/core/alignment.hpp"
#include "base/core/assert.hpp"
#include "base/core/config.hpp"
#include "base/core/types.hpp"

#include <cstdlib>

#if BASE_OS_MACOS
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

namespace base::platform {

void* heap_allocate(usize size, usize alignment) noexcept {
    BASE_ASSERT(is_power_of_two(alignment));
    if (alignment <= defaultHeapAlignment) {
        return std::malloc(size); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
    }
    // posix_memalign(), unlike std::aligned_alloc(), accepts sizes that are not a
    // multiple of the alignment.
    void* memory = nullptr;
    return posix_memalign(&memory, alignment, size) == 0 ? memory : nullptr;
}

// POSIX frees aligned and plain blocks alike.
void heap_free(void* memory, [[maybe_unused]] usize alignment) noexcept {
    std::free(memory); // NOLINT(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
}

usize heap_usable_size(const void* memory, [[maybe_unused]] usize alignment) noexcept {
    if (memory == nullptr) {
        return 0U;
    }
#if BASE_OS_MACOS
    return malloc_size(memory);
#else
    // glibc only reads the block, but its signature takes a non-const pointer.
    return malloc_usable_size(const_cast<void*>(memory)); // NOLINT(cppcoreguidelines-pro-type-const-cast)
#endif
}

} // namespace base::platform
