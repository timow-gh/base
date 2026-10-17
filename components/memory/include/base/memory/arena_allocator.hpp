#pragma once

#include "base/core/types.hpp"
#include "base/memory/arena.hpp"

#include <limits>
#include <new>
#include <type_traits>

namespace base::memory {

/**
 * \brief STL allocator that takes its memory from an Arena, so that standard containers
 * can live in one:
 * \code
 * std::vector<int, ArenaAllocator<int>> values(arena);
 * \endcode
 * The containers component has aliases for the common ones, e.g.
 * base::containers::ArenaVector.
 *
 * deallocate() does nothing; the memory comes back when the arena is rewound past it.
 * A growing std::vector therefore leaves each outgrown buffer behind, which its
 * geometric growth bounds to at most about twice the final capacity. Unlike
 * Arena::push(), containers run their elements' destructors, so any element type
 * works, but a container must be destroyed before its arena is rewound past it.
 *
 * Copying, moving or swapping containers never hands the allocator from one to the
 * other (all propagate_on_container_* traits are false, as for
 * std::pmr::polymorphic_allocator): a container never silently starts allocating from
 * another, possibly shorter-lived arena. Assigning between containers on different
 * arenas copies or moves the elements; swapping them is undefined behavior. Move
 * construction does take the source's arena.
 */
template <typename T>
class ArenaAllocator {
public:
    using value_type = T;
    using propagate_on_container_copy_assignment = std::false_type;
    using propagate_on_container_move_assignment = std::false_type;
    using propagate_on_container_swap = std::false_type;
    using is_always_equal = std::false_type;

    // Implicit, deviating from C.46, so that containers are constructed from the arena
    // itself: `std::vector<int, ArenaAllocator<int>> values(arena);`.
    ArenaAllocator(Arena& arena) noexcept
        : m_arena(&arena) {}

    // Implicit like std::allocator's: containers rebind their allocator to node types.
    template <typename U>
    ArenaAllocator(const ArenaAllocator<U>& other) noexcept
        : m_arena(&other.arena()) {}

    /**
     * \brief Uninitialized storage for `count` Ts.
     * \throws std::bad_array_new_length if `count` Ts do not fit in usize.
     * \throws std::bad_alloc when the arena is exhausted.
     */
    [[nodiscard]] T* allocate(usize count) {
        if (count > std::numeric_limits<usize>::max() / sizeof(T)) {
            throw std::bad_array_new_length();
        }
        void* memory = m_arena->allocate(count * sizeof(T), alignof(T));
        if (memory == nullptr) {
            throw std::bad_alloc();
        }
        return static_cast<T*>(memory);
    }

    // Even the most recent block is not given back: that would move the arena before
    // markers taken after the block, and rewinding to them would then fail.
    void deallocate([[maybe_unused]] T* memory, [[maybe_unused]] usize count) noexcept {}

    [[nodiscard]] Arena& arena() const noexcept { return *m_arena; }

private:
    Arena* m_arena;
};

/**
 * \brief Allocators are equal when they use the same arena: memory from one can then be
 * handed to the other.
 */
template <typename T, typename U>
[[nodiscard]] bool operator==(const ArenaAllocator<T>& lhs, const ArenaAllocator<U>& rhs) noexcept {
    return &lhs.arena() == &rhs.arena();
}

} // namespace base::memory
