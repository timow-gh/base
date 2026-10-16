#pragma once

#include "base/memory/arena.hpp"

#include <initializer_list>

namespace base::memory {

namespace detail {

/**
 * \brief Returns the calling thread's first scratch arena that is not in `conflicts`,
 * creating it on first use. Aborts if both scratch arenas conflict or the address
 * space for one cannot be reserved: there is no arena to fall back to.
 */
[[nodiscard]] Arena& scratch_arena(std::initializer_list<const Arena*> conflicts) noexcept;

} // namespace detail

/**
 * \brief Temporary memory from one of the calling thread's two scratch arenas, freed
 * when the scope ends.
 *
 * Scratch memory needs no arena from the caller, and nested scopes on the same arena
 * rewind in stack order. The one hazard is a function that returns its result in an
 * arena the caller passes in: that arena may be a scratch arena of the caller, and
 * then this scope's rewind would free the result. Passing every such output arena as
 * a conflict selects the other scratch arena. Two arenas suffice because a function
 * has at most one output arena in practice.
 *
 * \code
 * std::span<Edge> find_edges(Arena& output, const Mesh& mesh) {
 *     const ScratchScope scratch({&output});
 *     std::span<bool> visited = scratch.arena().push_array<bool>(mesh.vertexCount);
 *     ...
 *     return output.push_array<Edge>(edgeCount);
 * } // visited is freed, the edges are not
 * \endcode
 *
 * Each scratch arena reserves the default ArenaConfig, created on first use by a
 * thread and released when the thread exits.
 */
class ScratchScope {
public:
    explicit ScratchScope(std::initializer_list<const Arena*> conflicts = {}) noexcept
        : m_scope(detail::scratch_arena(conflicts)) {}

    ScratchScope(const ScratchScope&) = delete;
    ScratchScope& operator=(const ScratchScope&) = delete;
    ScratchScope(ScratchScope&&) = delete;
    ScratchScope& operator=(ScratchScope&&) = delete;
    ~ScratchScope() = default;

    [[nodiscard]] Arena& arena() const noexcept { return m_scope.arena(); }

private:
    ArenaScope m_scope;
};

} // namespace base::memory
