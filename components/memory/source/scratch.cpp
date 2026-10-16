#include "base/memory/scratch.hpp"

#include "base/core/assert.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace base::memory::detail {

namespace {

constexpr std::size_t scratchArenaCount = 2;

using ScratchArenas = std::array<std::optional<Arena>, scratchArenaCount>;

// Function-local, so threads that never use scratch memory reserve nothing.
[[nodiscard]] ScratchArenas& scratch_arenas() noexcept {
    thread_local ScratchArenas arenas;
    return arenas;
}

// Reported through the assertion handler even in release builds: the caller holds a
// reference, so there is no error value to return.
[[noreturn]] void scratch_failed(std::string_view message) noexcept {
    ::base::detail::assert_failed("scratch arena available", message);
    std::abort();
}

} // namespace

Arena& scratch_arena(std::initializer_list<const Arena*> conflicts) noexcept {
    for (std::optional<Arena>& slot : scratch_arenas()) {
        if (!slot.has_value()) {
            slot = Arena::create();
            if (!slot.has_value()) {
                scratch_failed("reserving the address space of a scratch arena failed");
            }
            return *slot;
        }
        if (std::ranges::find(conflicts, &*slot) == conflicts.end()) {
            return *slot;
        }
    }
    scratch_failed("every scratch arena is in the conflicts");
}

} // namespace base::memory::detail
