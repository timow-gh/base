#pragma once

#include "base/core/types.hpp"

namespace base::profile {

/**
 * \brief The heap allocations recorded with record_heap_allocation() and
 * record_heap_deallocation(). With base::profile_global_new the byte counts are the
 * usable sizes of the blocks, which may exceed the requested sizes.
 */
struct HeapStats {
    u64 allocationCount = 0;
    u64 deallocationCount = 0;
    u64 allocatedBytes = 0;
    u64 freedBytes = 0;
    /**
     * Allocated minus freed bytes: the heap memory in use. 0 rather than wrapping
     * around if more was freed than allocated, which happens when blocks that were
     * never recorded are freed through a recording operator delete, e.g. blocks the
     * C++ runtime DLL allocated on Windows.
     */
    u64 liveBytes = 0;
    // The highest liveBytes since the program started or since reset_heap_peak().
    u64 peakLiveBytes = 0;
};

/**
 * \brief Records a heap allocation of `bytes` in heap_stats() and charges it to the
 * calling thread's innermost open allocation zone (see BASE_PROFILE_SCOPE).
 *
 * base::profile_global_new calls it from its operator new. A program with an operator
 * new of its own calls it there instead, once the allocation succeeded, and calls
 * record_heap_deallocation() with the same byte count when the block is freed. It
 * guards both with `#if BASE_PROFILE_ALLOCATIONS` (base/core/config.hpp), so that its
 * other builds pay nothing, as with base::profile_global_new. The usable size of the
 * block (see base::platform::heap_usable_size()) suits both calls, since an unsized
 * operator delete can query it too.
 *
 * Thread-safe, lock-free and never allocates, so it is safe inside operator new, also
 * before main() and after it returned. A few relaxed atomic operations on one cache
 * line, which every allocating thread writes, so allocation-heavy threads slow each
 * other down. That is why it runs only in allocation builds, which report no times.
 */
void record_heap_allocation(usize bytes) noexcept;

/**
 * \brief Records that a block recorded by record_heap_allocation() with the same
 * `bytes` was freed. Thread-safe, see record_heap_allocation().
 */
void record_heap_deallocation(usize bytes) noexcept;

/**
 * \brief The heap allocations recorded so far. The fields are read one after another,
 * so while other threads allocate they need not agree with each other exactly.
 */
[[nodiscard]] HeapStats heap_stats() noexcept;

/**
 * \brief Lowers peakLiveBytes to the current liveBytes, so that heap_stats() reports
 * the peak of the phase that starts now, e.g. one frame or one request.
 */
void reset_heap_peak() noexcept;

} // namespace base::profile
