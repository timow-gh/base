#include "base/core/config.hpp"

// Compiled empty outside allocation builds, so linking base::profile_global_new costs
// nothing there: operator new stays the C++ runtime's, and a time measurement is not
// distorted by tracking. The target exists in every build, so consumers link it
// unconditionally and pick the measurement with base_PROFILING alone.
#if BASE_PROFILE_ALLOCATIONS

#include "base/core/types.hpp"
#include "base/platform/heap.hpp"
#include "base/profile/heap_tracking.hpp"

#include <new>

using namespace base::types;

namespace {

using base::platform::defaultHeapAlignment;

// Both directions record the usable size, so an unsized operator delete accounts for
// exactly what its operator new did. The extra query per call is acceptable because
// allocation builds report no times.
[[nodiscard]] void* allocate_tracked(usize size, usize alignment) noexcept {
    // operator new must return a unique pointer even for 0 bytes.
    void* memory = base::platform::heap_allocate(size == 0U ? 1U : size, alignment);
    if (memory != nullptr) {
        base::profile::record_heap_allocation(base::platform::heap_usable_size(memory, alignment));
    }
    return memory;
}

void free_tracked(void* memory, usize alignment) noexcept {
    if (memory == nullptr) {
        return;
    }
    base::profile::record_heap_deallocation(base::platform::heap_usable_size(memory, alignment));
    base::platform::heap_free(memory, alignment);
}

// The standard's contract for a throwing operator new: the new handler may free memory,
// so retry after each call of it, and throw once no handler is installed.
[[nodiscard]] void* allocate_or_throw(usize size, usize alignment) {
    for (;;) {
        if (void* memory = allocate_tracked(size, alignment)) {
            return memory;
        }
        auto* const handler = std::get_new_handler();
        if (handler == nullptr) {
            throw std::bad_alloc();
        }
        handler();
    }
}

// The standard defines the nothrow forms by the throwing ones, so a new handler that
// gives up by throwing std::bad_alloc also ends in nullptr.
[[nodiscard]] void* allocate_or_null(usize size, usize alignment) noexcept {
    try {
        return allocate_or_throw(size, alignment);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

} // namespace

// The replaceable forms of [new.delete]. The plain forms pass defaultHeapAlignment and
// the aligned forms their alignment, so a block always reaches the free function that
// matches its allocation.
// NOLINTBEGIN(cppcoreguidelines-owning-memory, misc-new-delete-overloads)

void* operator new(usize size) {
    return allocate_or_throw(size, defaultHeapAlignment);
}

void* operator new[](usize size) {
    return allocate_or_throw(size, defaultHeapAlignment);
}

void* operator new(usize size, const std::nothrow_t& /*tag*/) noexcept {
    return allocate_or_null(size, defaultHeapAlignment);
}

void* operator new[](usize size, const std::nothrow_t& /*tag*/) noexcept {
    return allocate_or_null(size, defaultHeapAlignment);
}

void* operator new(usize size, std::align_val_t alignment) {
    return allocate_or_throw(size, static_cast<usize>(alignment));
}

void* operator new[](usize size, std::align_val_t alignment) {
    return allocate_or_throw(size, static_cast<usize>(alignment));
}

void* operator new(usize size, std::align_val_t alignment, const std::nothrow_t& /*tag*/) noexcept {
    return allocate_or_null(size, static_cast<usize>(alignment));
}

void* operator new[](usize size, std::align_val_t alignment, const std::nothrow_t& /*tag*/) noexcept {
    return allocate_or_null(size, static_cast<usize>(alignment));
}

void operator delete(void* memory) noexcept {
    free_tracked(memory, defaultHeapAlignment);
}

void operator delete[](void* memory) noexcept {
    free_tracked(memory, defaultHeapAlignment);
}

void operator delete(void* memory, usize /*size*/) noexcept {
    free_tracked(memory, defaultHeapAlignment);
}

void operator delete[](void* memory, usize /*size*/) noexcept {
    free_tracked(memory, defaultHeapAlignment);
}

void operator delete(void* memory, const std::nothrow_t& /*tag*/) noexcept {
    free_tracked(memory, defaultHeapAlignment);
}

void operator delete[](void* memory, const std::nothrow_t& /*tag*/) noexcept {
    free_tracked(memory, defaultHeapAlignment);
}

void operator delete(void* memory, std::align_val_t alignment) noexcept {
    free_tracked(memory, static_cast<usize>(alignment));
}

void operator delete[](void* memory, std::align_val_t alignment) noexcept {
    free_tracked(memory, static_cast<usize>(alignment));
}

void operator delete(void* memory, usize /*size*/, std::align_val_t alignment) noexcept {
    free_tracked(memory, static_cast<usize>(alignment));
}

void operator delete[](void* memory, usize /*size*/, std::align_val_t alignment) noexcept {
    free_tracked(memory, static_cast<usize>(alignment));
}

void operator delete(void* memory, std::align_val_t alignment, const std::nothrow_t& /*tag*/) noexcept {
    free_tracked(memory, static_cast<usize>(alignment));
}

void operator delete[](void* memory, std::align_val_t alignment, const std::nothrow_t& /*tag*/) noexcept {
    free_tracked(memory, static_cast<usize>(alignment));
}

// NOLINTEND(cppcoreguidelines-owning-memory, misc-new-delete-overloads)

#endif
