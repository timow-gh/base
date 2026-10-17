#include "base/platform/virtual_memory.hpp"

#include "base/core/alignment.hpp"
#include "base/core/assert.hpp"
#include "base/core/types.hpp"

#include <sys/mman.h>
#include <unistd.h>

namespace base::platform {

namespace {

// Reserved address space must not count against the commit limit; only committed
// pages should. Not every POSIX system defines MAP_NORESERVE.
#ifdef MAP_NORESERVE
constexpr int reserveFlags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
#else
constexpr int reserveFlags = MAP_PRIVATE | MAP_ANONYMOUS;
#endif

// Only used in assertions, which release builds leave unevaluated.
[[nodiscard, maybe_unused]] bool is_page_range(const void* address, usize size) noexcept {
    return size > 0U && is_aligned(reinterpret_cast<uptr>(address), page_size()) && // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
           is_aligned(size, page_size());
}

[[nodiscard]] bool is_map_failed(const void* address) noexcept {
    return address == MAP_FAILED; // NOLINT(cppcoreguidelines-pro-type-cstyle-cast): system macro
}

} // namespace

usize page_size() noexcept {
    static const auto size = static_cast<usize>(sysconf(_SC_PAGESIZE));
    return size;
}

usize allocation_granularity() noexcept {
    return page_size();
}

void* reserve(usize size) noexcept {
    BASE_ASSERT_MSG(size > 0U && is_aligned(size, allocation_granularity()),
                    "reserve size must be a non-zero multiple of the allocation granularity");
    void* address = mmap(nullptr, size, PROT_NONE, reserveFlags, -1, 0);
    return is_map_failed(address) ? nullptr : address;
}

bool commit(void* address, usize size) noexcept {
    BASE_ASSERT(is_page_range(address, size));
    return mprotect(address, size, PROT_READ | PROT_WRITE) == 0;
}

void decommit(void* address, usize size) noexcept {
    BASE_ASSERT(is_page_range(address, size));
    // Mapping fresh PROT_NONE pages over the range drops the physical pages and
    // guarantees zeroed memory on the next commit on every POSIX system, unlike
    // madvise(), whose semantics differ between Linux and macOS.
    [[maybe_unused]] void* const remapped = mmap(address, size, PROT_NONE, reserveFlags | MAP_FIXED, -1, 0);
    BASE_ASSERT_MSG(!is_map_failed(remapped), "mmap(MAP_FIXED) failed to decommit");
}

void release(void* address, usize size) noexcept {
    BASE_ASSERT(address != nullptr);
    [[maybe_unused]] const int unmapped = munmap(address, size);
    BASE_ASSERT_MSG(unmapped == 0, "munmap failed");
}

} // namespace base::platform
