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
 * All code on a thread shares the same two scratch arenas. Nested scopes on the same
 * arena are fine for temporary memory, because they rewind in stack order. They are
 * not fine for a result: if a function writes its result into an arena it was given,
 * and the caller gave it its own scratch arena, the function's scope may pick that
 * same arena and free the result when it ends. So a function passes every arena it
 * writes results into as a conflict, and the scope picks the other scratch arena:
 *
 * \code
 * std::span<Edge> find_edges(Arena& output, const Mesh& mesh) {
 *     const ScratchScope scratch({&output}); // never the arena the result goes to
 *     std::span<bool> visited = scratch.arena().push_array<bool>(mesh.vertexCount);
 *     ...
 *     return output.push_array<Edge>(edgeCount);
 * } // frees `visited`, not the edges
 *
 * void build_graph(const Mesh& mesh) {
 *     const ScratchScope scratch;
 *     // The edges are only needed here, so they go into this scope's scratch arena.
 *     std::span<Edge> edges = find_edges(scratch.arena(), mesh);
 *     ...
 * } // frees the edges
 * \endcode
 *
 * A function cannot tell whether an arena it is given is a scratch arena, and does not
 * need to: a conflict that is not a scratch arena changes nothing.
 *
 * Each scratch arena reserves the default ArenaConfig, created on first use by a
 * thread and released when the thread exits.
 */
class ScratchScope {
public:
    /**
     * \param conflicts Arenas this scope must not use: every arena the function writes
     * results into. Aborts if both scratch arenas are among them.
     */
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
