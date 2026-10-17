#pragma once

#include "base/memory/arena_allocator.hpp"

#include <vector>

namespace base::containers {

// A std::vector in an Arena, constructed from the arena: `ArenaVector<int> values(arena);`.
// See base::memory::ArenaAllocator for the lifetime and assignment rules.
template <typename T>
using ArenaVector = std::vector<T, memory::ArenaAllocator<T>>;

} // namespace base::containers
