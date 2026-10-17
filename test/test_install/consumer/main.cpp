#include <base/containers/arena_vector.hpp>
#include <base/core/config.hpp>
#include <base/memory/arena.hpp>
#include <base/profile/heap_tracking.hpp>

#include <cstdio>
#include <new>
#include <optional>

int main() {
    std::optional<base::memory::Arena> arena = base::memory::Arena::create();
    if (!arena) {
        std::puts("Arena::create failed");
        return 1;
    }

    const int* value = arena->push<int>(42);
    if (value == nullptr || *value != 42) {
        std::puts("Arena::push failed");
        return 1;
    }

    base::containers::ArenaVector<int> values(*arena);
    values.assign({1, 2, 3});
    if (values.back() != 3 || &values.get_allocator().arena() != &*arena) {
        std::puts("ArenaVector failed");
        return 1;
    }

    const base::profile::HeapStats before = base::profile::heap_stats();
    base::profile::record_heap_allocation(16);
    base::profile::record_heap_deallocation(16);
    if (base::profile::heap_stats().allocationCount <= before.allocationCount) {
        std::puts("base::profile::record_heap_allocation failed");
        return 1;
    }

#if BASE_PROFILE_ALLOCATIONS
    // A direct call: unlike a new-expression, the compiler may not elide it. Only
    // the installed base::profile_global_new can make it count.
    const base::profile::HeapStats beforeNew = base::profile::heap_stats();
    void* block = ::operator new(sizeof(int));
    const base::profile::HeapStats afterNew = base::profile::heap_stats();
    ::operator delete(block);
    if (afterNew.allocationCount <= beforeNew.allocationCount) {
        std::puts("base::profile_global_new does not replace operator new");
        return 1;
    }
#endif

    std::puts("base::memory, base::containers and base::profile work");
    return 0;
}
