#include "base/platform/virtual_memory.hpp"

#include "base/core/alignment.hpp"
#include "base/core/assert.hpp"
#include "base/core/types.hpp"

#include <windows.h>

namespace base::platform {

namespace {

const SYSTEM_INFO& system_info() noexcept {
    static const SYSTEM_INFO info = [] {
        SYSTEM_INFO result{};
        GetSystemInfo(&result);
        return result;
    }();
    return info;
}

// Only used in assertions, which release builds leave unevaluated.
[[nodiscard, maybe_unused]] bool is_page_range(const void* address, usize size) noexcept {
    return size > 0U && is_aligned(reinterpret_cast<uptr>(address), page_size()) && // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
           is_aligned(size, page_size());
}

} // namespace

usize page_size() noexcept {
    return system_info().dwPageSize;
}

usize allocation_granularity() noexcept {
    return system_info().dwAllocationGranularity;
}

void* reserve(usize size) noexcept {
    BASE_ASSERT_MSG(size > 0U && is_aligned(size, allocation_granularity()),
                    "reserve size must be a non-zero multiple of the allocation granularity");
    return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
}

bool commit(void* address, usize size) noexcept {
    BASE_ASSERT(is_page_range(address, size));
    return VirtualAlloc(address, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

void decommit(void* address, usize size) noexcept {
    BASE_ASSERT(is_page_range(address, size));
    [[maybe_unused]] const BOOL decommitted = VirtualFree(address, size, MEM_DECOMMIT);
    BASE_ASSERT_MSG(decommitted != 0, "VirtualFree(MEM_DECOMMIT) failed");
}

void release(void* address, [[maybe_unused]] usize size) noexcept {
    BASE_ASSERT(address != nullptr);
    // MEM_RELEASE always frees the whole reservation and requires a size of 0.
    [[maybe_unused]] const BOOL released = VirtualFree(address, 0, MEM_RELEASE);
    BASE_ASSERT_MSG(released != 0, "VirtualFree(MEM_RELEASE) failed");
}

} // namespace base::platform
