#pragma once

#include "base/core/types.hpp"
#include "base/memory/arena.hpp"

#include <initializer_list>

namespace base::memory {

/**
 * \brief How prepare_scratch_arenas() sets up each of a thread's scratch arenas.
 */
struct ScratchConfig {
    // Reservation and commit step of each scratch arena.
    ArenaConfig arena;
    // Bytes committed up front in each scratch arena, see Arena::commit_at_least().
    usize initialCommit = 0;
    Prefault prefault = Prefault::No;
};

/**
 * \brief Creates the calling thread's two scratch arenas with `config` and commits
 * `config.initialCommit` bytes in each, so that a ScratchArena never needs a commit
 * call up to that size.
 *
 * Optional: without it, a thread's scratch arenas are created on first use with the
 * default ArenaConfig. Call it at the start of each thread that wants it, before the
 * thread's first ScratchArena; scratch arenas that already exist keep their
 * reservation and only get the commit.
 *
 * \return false if a reservation or the commit fails. Unlike the creation on first
 * use, which aborts, this lets the application report the failure.
 */
[[nodiscard]] bool prepare_scratch_arenas(const ScratchConfig& config = {}) noexcept;

namespace detail {

/**
 * \brief Returns the calling thread's first scratch arena that is not in `conflicts`,
 * creating it on first use. Aborts if both scratch arenas conflict or the address
 * space for one cannot be reserved: there is no arena to fall back to.
 */
[[nodiscard]] Arena& scratch_arena(std::initializer_list<const Arena*> conflicts) noexcept;

} // namespace detail

/**
 * \brief Borrows one of the calling thread's two scratch arenas for temporary memory.
 * Everything allocated through it is freed when the ScratchArena is destroyed.
 *
 * Not an arena of its own: it neither owns nor creates the arena it hands out, see
 * arena(). All code on a thread borrows from the same two scratch arenas. Nested
 * ScratchArenas on the same arena are fine for temporary memory, because they rewind
 * in stack order. They are not fine for a result: if a function writes its result into
 * an arena it was given, and the caller gave it its own scratch arena, the function's
 * ScratchArena may borrow that same arena and free the result when it is destroyed. So
 * a function passes every arena it writes results into as a conflict, and the
 * ScratchArena borrows the other scratch arena:
 *
 * \code
 * std::span<Edge> find_edges(Arena& output, const Mesh& mesh) {
 *     const ScratchArena scratch({&output}); // never the arena the result goes to
 *     std::span<bool> visited = scratch.arena().push_array<bool>(mesh.vertexCount);
 *     ...
 *     return output.push_array<Edge>(edgeCount);
 * } // frees `visited`, not the edges
 *
 * void build_graph(const Mesh& mesh) {
 *     const ScratchArena scratch;
 *     // The edges are only needed here, so they go into this scratch arena.
 *     std::span<Edge> edges = find_edges(scratch.arena(), mesh);
 *     ...
 * } // frees the edges
 * \endcode
 *
 * A function cannot tell whether an arena it is given is a scratch arena, and does not
 * need to: a conflict that is not a scratch arena changes nothing.
 *
 * A thread's scratch arenas are created on first use with the default ArenaConfig, or
 * up front by prepare_scratch_arenas(), and released when the thread exits.
 */
class ScratchArena {
public:
    /**
     * \param conflicts Arenas this ScratchArena must not borrow: every arena the
     * function writes results into. Aborts if both scratch arenas are among them.
     */
    explicit ScratchArena(std::initializer_list<const Arena*> conflicts = {}) noexcept
        : m_scope(detail::scratch_arena(conflicts)) {}

    ScratchArena(const ScratchArena&) = delete;
    ScratchArena& operator=(const ScratchArena&) = delete;
    ScratchArena(ScratchArena&&) = delete;
    ScratchArena& operator=(ScratchArena&&) = delete;
    ~ScratchArena() = default;

    [[nodiscard]] Arena& arena() const noexcept { return m_scope.arena(); }

private:
    ArenaScope m_scope;
};

} // namespace base::memory
