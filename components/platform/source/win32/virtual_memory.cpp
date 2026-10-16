#include "base/platform/virtual_memory.hpp"

#include "base/core/assert.hpp"
#include "base/core/bits.hpp"

#include <cstdint>

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

[[nodiscard]] bool is_page_range(const void* address, std::size_t size) noexcept {
    return size > 0U && is_aligned(reinterpret_cast<std::uintptr_t>(address), page_size()) && // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
           is_aligned(size, page_size());
}

} // namespace

std::size_t page_size() noexcept {
    return system_info().dwPageSize;
}

std::size_t allocation_granularity() noexcept {
    return system_info().dwAllocationGranularity;
}

void* reserve(std::size_t size) noexcept {
    BASE_ASSERT_MSG(size > 0U && is_aligned(size, allocation_granularity()),
                    "reserve size must be a non-zero multiple of the allocation granularity");
    return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
}

bool commit(void* address, std::size_t size) noexcept {
    BASE_ASSERT(is_page_range(address, size));
    return VirtualAlloc(address, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

void decommit(void* address, std::size_t size) noexcept {
    BASE_ASSERT(is_page_range(address, size));
    BASE_VERIFY_MSG(VirtualFree(address, size, MEM_DECOMMIT) != 0, "VirtualFree(MEM_DECOMMIT) failed");
}

void release(void* address, [[maybe_unused]] std::size_t size) noexcept {
    BASE_ASSERT(address != nullptr);
    // MEM_RELEASE always frees the whole reservation and requires a size of 0.
    BASE_VERIFY_MSG(VirtualFree(address, 0, MEM_RELEASE) != 0, "VirtualFree(MEM_RELEASE) failed");
}

} // namespace base::platform
