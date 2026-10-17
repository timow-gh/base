#pragma once

#include "base/core/types.hpp"
#include "base/memory/arena.hpp"

#include <limits>
#include <new>
#include <type_traits>

namespace base::memory {

namespace detail {

/**
 * \brief Throw the errors of ArenaAllocator::allocate(). Defined in a source file, as the
 * standard library does with its own throw helpers, so they are never inlined.
 *
 * With the throws inline, MSVC inlines vector's whole reallocation path into the
 * function behind push_back, which then grows too large to be inlined itself: every
 * push_back becomes a call, about 2.5× slower than with std::allocator.
 */
[[noreturn]] void throw_bad_alloc();
[[noreturn]] void throw_bad_array_new_length();

} // namespace detail

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
 * Containers therefore suit lifetimes that end with the arena or an ArenaScope: build,
 * use, discard. A long-lived container that keeps erasing and inserting, a string that
 * keeps outgrowing its capacity, or a std::deque or std::list used as a queue takes new
 * memory for every block and never reuses the old one, so its arena grows for as long
 * as the container lives.
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
        // Here and not in the class body, which must accept incomplete Ts, e.g. a node
        // holding an ArenaVector of its own type.
        static_assert(alignof(T) <= maxArenaAlignment, "an arena cannot align beyond maxArenaAlignment");
        if (count > std::numeric_limits<usize>::max() / sizeof(T)) {
            detail::throw_bad_array_new_length();
        }
        void* memory = m_arena->allocate(count * sizeof(T), alignof(T));
        if (memory == nullptr) {
            detail::throw_bad_alloc();
        }
        return static_cast<T*>(memory);
    }

    // Not even the most recent block is given back: containers free a buffer only after
    // allocating its replacement, so the freed block is almost never the most recent one.
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
