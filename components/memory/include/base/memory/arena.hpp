#pragma once

#include "base/core/assert.hpp"
#include "base/core/units.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace base::memory {

/**
 * \brief Types an Arena may construct. The arena never runs destructors, so a type
 * whose destructor releases something (std::string, std::vector, ...) would leak.
 */
template <typename T>
concept TriviallyDestructible = std::is_trivially_destructible_v<T>;

/**
 * \brief Debug fill pattern of memory handed out by Arena::allocate() and not yet
 * initialized. Reading `0xCDCDCDCD...` means reading uninitialized arena memory.
 */
inline constexpr std::byte arenaUninitializedByte{0xCD};

/**
 * \brief Debug fill pattern of memory rewound by Arena::reset() or Arena::rewind_to().
 * Reading `0xDDDDDDDD...` means using a pointer into memory that was already freed.
 */
inline constexpr std::byte arenaFreedByte{0xDD};

// The reservation is the arena's hard capacity, since it never moves or chains blocks,
// but it is address space only: no RAM, no commit charge, no page tables. So the
// default is larger than any arena can realistically commit, while ~2000 such arenas
// still fit in the 128 TiB of a 64-bit process. Many small arenas, or environments
// that limit address space (ulimit -v, Valgrind), should state their size, see
// arena_bytes_for(). 64 KiB commit steps balance the number of commit calls against
// committed-but-unused memory.
static_assert(sizeof(void*) >= sizeof(std::uint64_t), "the default arena reservation assumes a 64-bit address space");
inline constexpr std::size_t defaultArenaReserveSize = gib(64);
inline constexpr std::size_t defaultArenaCommitSize = kib(64);

/**
 * \brief Bytes an arena must hold for `count` objects of T, pushed with one
 * push_array<T>(count) or with `count` consecutive push<T>() calls: the objects plus
 * the worst-case alignment padding before the first one.
 *
 * Consecutive Ts need no padding between them because sizeof(T) is a multiple of
 * alignof(T), so the results of several calls add up to a safe total:
 * \code
 * constexpr std::size_t meshArenaSize = arena_bytes_for<Vertex>(10'000) + arena_bytes_for<Index>(30'000);
 * std::optional<Arena> arena = Arena::create(meshArenaSize);
 * \endcode
 */
template <typename T>
[[nodiscard]] constexpr std::size_t arena_bytes_for(std::size_t count = 1) noexcept {
    constexpr std::size_t maxPadding = alignof(T) - 1U;
    if (count == 0U) {
        return 0U;
    }
    BASE_ASSERT_MSG(count <= (std::numeric_limits<std::size_t>::max() - maxPadding) / sizeof(T),
                    "arena size overflows std::size_t");
    return (count * sizeof(T)) + maxPadding;
}

/**
 * \brief Sizes of an arena. Both are rounded up to what the OS requires.
 */
struct ArenaConfig {
    // Address space only; physical memory is committed in `commitSize` steps as the
    // arena grows. Rounded up to the allocation granularity.
    std::size_t reserveSize = defaultArenaReserveSize;
    // Rounded up to a multiple of the page size. Larger steps mean fewer commit calls.
    std::size_t commitSize = defaultArenaCommitSize;
};

/**
 * \brief Whether Arena::commit_at_least() also writes to every page it commits, called
 * prefaulting.
 *
 * Committing only promises the memory. The OS maps a zeroed physical page on the
 * first write to it, a page fault of roughly a microsecond per 4 KiB page.
 * Prefault::Yes takes all those faults now, so later allocations never pause for one.
 * It saves no time overall, since the zeroing just happens earlier, and it puts memory
 * into RAM that may never be used. Worth it for latency-critical code with a known
 * peak, e.g. a frame loop or a benchmark.
 */
enum class Prefault : std::uint8_t {
    No,  // Commit only; each page is mapped on its first write.
    Yes, // Also write to every page now, so no page faults occur later.
};

/**
 * \brief A linear (bump) allocator over one contiguous range of reserved virtual memory.
 *
 * Allocation is a pointer bump plus, when the committed part is exhausted, one commit
 * call. Memory is never freed individually: reset() and rewind_to() move the arena
 * back, and destroying it releases everything at once. Destructors are never run,
 * which is why push() only accepts trivially destructible types.
 *
 * Pointers stay valid until the arena is rewound past them, because the arena grows
 * in place and never moves.
 *
 * Rewinding does not clear memory. In debug builds (see debug_checks_enabled())
 * rewound memory is filled with arenaFreedByte and newly allocated memory with
 * arenaUninitializedByte, and an allocation asserts if memory it reuses was written
 * after it was freed. Under AddressSanitizer every byte that is not handed out is
 * poisoned, so ASan reports reads and writes of freed arena memory.
 *
 * Not thread-safe: use one arena per thread or synchronize externally.
 */
class Arena {
public:
    /**
     * \brief A position in an arena, see mark_position() and rewind_to().
     */
    struct Marker {
        std::size_t offset = 0;
    };

    /**
     * \brief Reserves the address space for a new arena. Nothing is committed yet.
     *
     * The reservation is rounded up to the allocation granularity (64 KiB on
     * Windows, the page size elsewhere), so even a tiny arena reserves that much
     * address space, and commits at least one page once it is used.
     *
     * \return std::nullopt if the address space cannot be reserved.
     */
    [[nodiscard]] static std::optional<Arena> create(const ArenaConfig& config = {}) noexcept;

    /**
     * \brief Like create(const ArenaConfig&) with the default commit size, e.g.
     * `Arena::create(mib(64))` or `Arena::create(arena_bytes_for<Particle>(1000))`.
     */
    [[nodiscard]] static std::optional<Arena> create(std::size_t reserveSize) noexcept;

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&& other) noexcept;
    Arena& operator=(Arena&& other) noexcept;
    ~Arena();

    /**
     * \brief Allocates `size` uninitialized bytes.
     * \param alignment A power of two, at most the page size.
     * \return nullptr when the reservation or the system's memory is exhausted.
     */
    [[nodiscard]] void* allocate(std::size_t size, std::size_t alignment) noexcept;

    /**
     * \brief Like allocate(), but the bytes are zero. Memory that was never handed out
     * is zero already, so only reused bytes are cleared.
     */
    [[nodiscard]] void* allocate_zeroed(std::size_t size, std::size_t alignment) noexcept;

    /**
     * \brief Grows the block at `memory` from `oldSize` to `newSize` bytes without moving it.
     *
     * Only possible for the most recent allocation, and only while no mark_position() was
     * taken after it: rewinding to such a marker would free the grown part. The added
     * bytes are uninitialized. O(1), plus a commit call when the committed part is
     * exhausted.
     *
     * \param newSize At least `oldSize`.
     * \return false, leaving the arena unchanged, if the block cannot grow in place.
     */
    [[nodiscard]] bool try_grow(void* memory, std::size_t oldSize, std::size_t newSize) noexcept;

    /**
     * \brief Grows the block at `memory` in place if possible (see try_grow()), otherwise
     * allocates `newSize` bytes and copies the first `oldSize` bytes over.
     *
     * The copy is a plain byte copy, so the block must hold trivially copyable data.
     * A copied-from block is not reused: it stays allocated until the arena is
     * rewound past it. Growing geometrically (e.g. doubling) keeps that waste below the
     * final size.
     *
     * \param memory nullptr (with `oldSize` 0) allocates a new block.
     * \param newSize At least `oldSize`.
     * \param alignment The alignment `memory` was allocated with.
     * \return nullptr, leaving the old block intact, when the arena is exhausted.
     */
    [[nodiscard]] void* reallocate(void* memory, std::size_t oldSize, std::size_t newSize, std::size_t alignment) noexcept;

    /**
     * \brief Commits the first `size` bytes of the arena now, so that allocations up to
     * that size never need a commit call. Nothing is allocated; used() is unchanged.
     *
     * Meant for setup, when the peak usage is known, e.g. a tool that commits its
     * permanent arena once at startup. One commit call, rounded up to the commit step;
     * with Prefault::Yes also one write per page, O(size / page size).
     *
     * \param prefault Prefault::Yes also maps the pages into RAM now, see Prefault.
     * \return false, leaving the arena unchanged, if `size` exceeds the reservation or
     * the system cannot commit the memory.
     */
    [[nodiscard]] bool commit_at_least(std::size_t size, Prefault prefault = Prefault::No) noexcept;

    /**
     * \brief Constructs a T from `args`. It is never destroyed.
     * \return nullptr when the arena is exhausted.
     */
    template <TriviallyDestructible T, typename... Args>
        requires std::constructible_from<T, Args...>
    [[nodiscard]] T* push(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        void* memory = allocate(sizeof(T), alignof(T));
        if (memory == nullptr) {
            return nullptr;
        }
        return std::construct_at(static_cast<T*>(memory), std::forward<Args>(args)...);
    }

    /**
     * \brief Allocates `count` value-initialized (for scalars: zeroed) Ts.
     * \return An empty span when `count` is 0 or the arena is exhausted.
     */
    template <TriviallyDestructible T>
        requires std::default_initializable<T>
    [[nodiscard]] std::span<T> push_array(std::size_t count) noexcept(std::is_nothrow_default_constructible_v<T>) {
        if (count == 0U || count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            return {};
        }
        void* memory = allocate(count * sizeof(T), alignof(T));
        if (memory == nullptr) {
            return {};
        }
        T* first = static_cast<T*>(memory);
        std::uninitialized_value_construct_n(first, count);
        return {first, count};
    }

    /**
     * \brief Marks the current position, to return to it later with rewind_to().
     *
     * Non-const because it ends try_grow() for the allocations made before it.
     */
    [[nodiscard]] Marker mark_position() noexcept {
        m_lastAllocation = noLastAllocation;
        return Marker{m_offset};
    }

    /**
     * \brief Frees everything allocated after `marker` was taken. O(1) in release builds.
     * \param marker A marker of this arena that is not past the current position.
     */
    void rewind_to(Marker marker) noexcept;

    /**
     * \brief Frees everything. Committed memory stays committed for reuse.
     */
    void reset() noexcept { rewind_to(Marker{}); }

    /** \brief Bytes handed out, including alignment padding. */
    [[nodiscard]] std::size_t used() const noexcept { return m_offset; }

    /** \brief Bytes backed by physical memory. Never shrinks. */
    [[nodiscard]] std::size_t committed() const noexcept { return m_committed; }

    /** \brief Bytes of reserved address space, the hard capacity of the arena. */
    [[nodiscard]] std::size_t reserved() const noexcept { return m_reserved; }

    /**
     * \brief True if the library was built with the debug fill patterns and the
     * use-after-free check (BASE_ARENA_DEBUG, on by default when assertions are on).
     */
    [[nodiscard]] static bool debug_checks_enabled() noexcept;

private:
    enum class Fill : std::uint8_t {
        Uninitialized,
        Zeroed,
    };

    static constexpr std::size_t noLastAllocation = std::numeric_limits<std::size_t>::max();

    Arena(std::byte* base, std::size_t reserved, std::size_t commitStep) noexcept;

    [[nodiscard]] void* allocate_impl(std::size_t size, std::size_t alignment, Fill fill) noexcept;
    // Moves the position to `end` and hands out [begin, end); [m_offset, begin) is padding.
    [[nodiscard]] bool hand_out(std::size_t begin, std::size_t end, Fill fill) noexcept;
    [[nodiscard]] bool commit_up_to(std::size_t end) noexcept;
    void touch_pages(std::size_t begin, std::size_t end) noexcept;
    void release_reservation() noexcept;

    std::byte* m_base = nullptr;
    std::size_t m_reserved = 0;
    std::size_t m_committed = 0;
    std::size_t m_commitStep = 0;
    std::size_t m_offset = 0;
    // The largest offset ever handed out. Bytes below it may hold old data; bytes
    // above it are still zero from the OS.
    std::size_t m_highWater = 0;
    // Offset of the allocation that try_grow() may extend, or noLastAllocation.
    std::size_t m_lastAllocation = noLastAllocation;
};

/**
 * \brief Scratch region: everything allocated from the arena during the scope's
 * lifetime is freed when the scope ends.
 *
 * \code
 * void parse(Arena& arena) {
 *     const ArenaScope scratch(arena);
 *     auto tokens = arena.push_array<Token>(count);
 *     ...
 * } // tokens are freed here
 * \endcode
 *
 * Memory that must outlive the scope, such as a result or a container created before
 * the scope, must not come from the same arena while the scope is active: the rewind
 * frees it. Put it in another arena, see ScratchArena.
 */
class ArenaScope {
public:
    explicit ArenaScope(Arena& arena) noexcept
        : m_arena(&arena)
        , m_marker(arena.mark_position()) {}

    ArenaScope(const ArenaScope&) = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;
    ArenaScope(ArenaScope&&) = delete;
    ArenaScope& operator=(ArenaScope&&) = delete;

    ~ArenaScope() { m_arena->rewind_to(m_marker); }

    [[nodiscard]] Arena& arena() const noexcept { return *m_arena; }

private:
    Arena* m_arena;
    Arena::Marker m_marker;
};

namespace detail {

/**
 * \brief Returns the index of the first byte in `bytes` that is not arenaFreedByte,
 * i.e. freed arena memory that was written after it was freed.
 */
[[nodiscard]] std::optional<std::size_t> find_modified_freed_byte(std::span<const std::byte> bytes) noexcept;

} // namespace detail

} // namespace base::memory
