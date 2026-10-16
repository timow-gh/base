#pragma once

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
 * \brief Debug fill pattern of memory rewound by Arena::reset() or Arena::restore().
 * Reading `0xDDDDDDDD...` means using a pointer into memory that was already freed.
 */
inline constexpr std::byte arenaFreedByte{0xDD};

// 64 GiB of address space costs nothing until it is committed, and is more than
// any single arena should need; 64 KiB commit steps balance syscalls against waste.
inline constexpr std::size_t defaultArenaReserveSize = gib(64);
inline constexpr std::size_t defaultArenaCommitSize = kib(64);

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
 * \brief A linear (bump) allocator over one contiguous range of reserved virtual memory.
 *
 * Allocation is a pointer bump plus, when the committed part is exhausted, one commit
 * call. Memory is never freed individually: reset() and restore() rewind the arena,
 * and destroying it releases everything at once. Destructors are never run, which is
 * why push() only accepts trivially destructible types.
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
     * \brief A position in an arena, see save() and restore().
     */
    struct Marker {
        std::size_t offset = 0;
    };

    /**
     * \brief Reserves the address space for a new arena. Nothing is committed yet.
     * \return std::nullopt if the address space cannot be reserved.
     */
    [[nodiscard]] static std::optional<Arena> create(const ArenaConfig& config = {}) noexcept;

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
     * \brief Returns the current position, to rewind to it later with restore().
     */
    [[nodiscard]] Marker save() const noexcept { return Marker{m_offset}; }

    /**
     * \brief Frees everything allocated after `marker` was saved. O(1) in release builds.
     * \param marker A marker of this arena that is not past the current position.
     */
    void restore(Marker marker) noexcept;

    /**
     * \brief Frees everything. Committed memory stays committed for reuse.
     */
    void reset() noexcept { restore(Marker{}); }

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

    Arena(std::byte* base, std::size_t reserved, std::size_t commitStep) noexcept;

    [[nodiscard]] void* allocate_impl(std::size_t size, std::size_t alignment, Fill fill) noexcept;
    [[nodiscard]] bool commit_up_to(std::size_t end) noexcept;
    void release_reservation() noexcept;

    std::byte* m_base = nullptr;
    std::size_t m_reserved = 0;
    std::size_t m_committed = 0;
    std::size_t m_commitStep = 0;
    std::size_t m_offset = 0;
    // The largest offset ever handed out. Bytes below it may hold old data; bytes
    // above it are still zero from the OS.
    std::size_t m_highWater = 0;
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
 */
class ArenaScope {
public:
    explicit ArenaScope(Arena& arena) noexcept
        : m_arena(&arena)
        , m_marker(arena.save()) {}

    ArenaScope(const ArenaScope&) = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;
    ArenaScope(ArenaScope&&) = delete;
    ArenaScope& operator=(ArenaScope&&) = delete;

    ~ArenaScope() { m_arena->restore(m_marker); }

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
