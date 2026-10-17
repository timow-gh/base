#pragma once

#include "base/memory/arena_allocator.hpp"

#include <functional>
#include <set>

namespace base::containers {

// A std::set whose nodes live in an Arena.
// See base::memory::ArenaAllocator for the lifetime and assignment rules.
template <typename Key, typename Compare = std::less<Key>>
using ArenaSet = std::set<Key, Compare, memory::ArenaAllocator<Key>>;

} // namespace base::containers
