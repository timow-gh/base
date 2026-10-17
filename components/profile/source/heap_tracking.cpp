#include "base/profile/heap_tracking.hpp"

#include "base/core/atomic.hpp"
#include "base/core/types.hpp"
#include "base/profile/profile.hpp"

#include <array>
#include <atomic>
#include <cstddef>

namespace base::profile {

namespace {

// 64 bytes, not std::hardware_destructive_interference_size, which GCC warns about
// (-Winterference-size) because its value depends on the target flags.
constexpr usize cacheLineSize = 64;

// All counters on one cache line of their own. Every operator new and delete of every
// thread writes them, so they contend however they are laid out: a line per counter
// would only make each call fetch more lines. The alignment keeps unrelated data off
// the line. The live bytes are derived from allocated and freed bytes instead of being
// counted, which saves an atomic operation per call.
struct alignas(cacheLineSize) HeapCounters {
    std::atomic<u64> allocationCount{0};
    std::atomic<u64> deallocationCount{0};
    std::atomic<u64> allocatedBytes{0};
    std::atomic<u64> freedBytes{0};
    std::atomic<u64> peakLiveBytes{0};
    // Fills the line by hand: MSVC warns (C4324) when alignas pads a structure.
    static constexpr usize counterCount = 5;
    std::array<std::byte, cacheLineSize - (counterCount * sizeof(std::atomic<u64>))> padding{};
};
// Also fails when a counter is added without raising counterCount.
static_assert(sizeof(HeapCounters) == cacheLineSize);

// A mutable global, against Core Guidelines I.2: operator new reports to it from every
// thread, also before main() and after it returned, so it must live for the whole
// program and be constant-initialized, which a function-local static is not.
constinit HeapCounters counters; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// Callers read `freed` before `allocated`: both only grow, so the frees of blocks
// allocated in between cannot make the difference too small. Exact while one thread
// allocates; with several, frees in flight can make it a little too large. Clamped
// instead of wrapping around, because unrecorded blocks freed through a recording
// operator delete (see HeapStats::liveBytes) can make freed exceed allocated.
[[nodiscard]] u64 live_bytes(u64 allocated, u64 freed) noexcept {
    return allocated > freed ? allocated - freed : 0U;
}

} // namespace

void record_heap_allocation(usize bytes) noexcept {
    counters.allocationCount.fetch_add(1U, std::memory_order_relaxed);
    const u64 freed = counters.freedBytes.load(std::memory_order_relaxed);
    const u64 allocated = counters.allocatedBytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    base::atomic_fetch_max(counters.peakLiveBytes, live_bytes(allocated, freed));
    ++detail::allocationClock.count;
    detail::allocationClock.bytes += bytes;
}

void record_heap_deallocation(usize bytes) noexcept {
    counters.deallocationCount.fetch_add(1U, std::memory_order_relaxed);
    counters.freedBytes.fetch_add(bytes, std::memory_order_relaxed);
}

HeapStats heap_stats() noexcept {
    const u64 freed = counters.freedBytes.load(std::memory_order_relaxed);
    const u64 allocated = counters.allocatedBytes.load(std::memory_order_relaxed);
    return HeapStats{.allocationCount = counters.allocationCount.load(std::memory_order_relaxed),
                     .deallocationCount = counters.deallocationCount.load(std::memory_order_relaxed),
                     .allocatedBytes = allocated,
                     .freedBytes = freed,
                     .liveBytes = live_bytes(allocated, freed),
                     .peakLiveBytes = counters.peakLiveBytes.load(std::memory_order_relaxed)};
}

void reset_heap_peak() noexcept {
    counters.peakLiveBytes.store(heap_stats().liveBytes, std::memory_order_relaxed);
}

} // namespace base::profile
