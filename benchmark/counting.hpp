#pragma once

#include "base/core/types.hpp"

#include <algorithm>
#include <memory>
#include <memory_resource>

namespace base::bench {

// What a CountingAllocator or CountingResource saw.
struct AllocationStats {
    usize liveBytes = 0;
    usize peakBytes = 0;
    usize totalBytes = 0;
    usize allocations = 0;

    void on_allocate(usize bytes) noexcept {
        liveBytes += bytes;
        peakBytes = std::max(peakBytes, liveBytes);
        totalBytes += bytes;
        ++allocations;
    }

    void on_deallocate(usize bytes) noexcept { liveBytes -= bytes; }
};

/**
 * \brief std::allocator that records every call in an AllocationStats.
 *
 * Only for the untimed footprint pass: in the timed loop the bookkeeping would be
 * charged to std::allocator, which the arena variants do not pay.
 */
template <typename T>
class CountingAllocator {
public:
    using value_type = T;

    explicit CountingAllocator(AllocationStats& stats) noexcept
        : m_stats(&stats) {}

    // Implicit like std::allocator's: containers rebind their allocator to node types.
    template <typename U>
    CountingAllocator(const CountingAllocator<U>& other) noexcept
        : m_stats(&other.stats()) {}

    [[nodiscard]] T* allocate(usize count) {
        T* memory = std::allocator<T>().allocate(count);
        m_stats->on_allocate(count * sizeof(T));
        return memory;
    }

    void deallocate(T* memory, usize count) noexcept {
        m_stats->on_deallocate(count * sizeof(T));
        std::allocator<T>().deallocate(memory, count);
    }

    [[nodiscard]] AllocationStats& stats() const noexcept { return *m_stats; }

private:
    AllocationStats* m_stats;
};

template <typename T, typename U>
[[nodiscard]] bool operator==(const CountingAllocator<T>& lhs, const CountingAllocator<U>& rhs) noexcept {
    return &lhs.stats() == &rhs.stats();
}

/**
 * \brief Memory resource that forwards to `upstream` and records every call, the pmr
 * counterpart of CountingAllocator.
 */
class CountingResource final : public std::pmr::memory_resource {
public:
    explicit CountingResource(std::pmr::memory_resource& upstream) noexcept
        : m_upstream(&upstream) {}

    CountingResource(const CountingResource&) = delete;
    CountingResource& operator=(const CountingResource&) = delete;
    CountingResource(CountingResource&&) = delete;
    CountingResource& operator=(CountingResource&&) = delete;
    ~CountingResource() override = default;

    [[nodiscard]] const AllocationStats& stats() const noexcept { return m_stats; }

private:
    void* do_allocate(usize bytes, usize alignment) override {
        void* memory = m_upstream->allocate(bytes, alignment);
        m_stats.on_allocate(bytes);
        return memory;
    }

    void do_deallocate(void* memory, usize bytes, usize alignment) override {
        m_stats.on_deallocate(bytes);
        m_upstream->deallocate(memory, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    std::pmr::memory_resource* m_upstream;
    AllocationStats m_stats;
};

} // namespace base::bench
