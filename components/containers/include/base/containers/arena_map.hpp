#pragma once

#include "base/memory/arena_allocator.hpp"

#include <functional>
#include <map>
#include <utility>

namespace base::containers {

// A std::map whose nodes live in an Arena.
// See base::memory::ArenaAllocator for the lifetime and assignment rules.
template <typename Key, typename Value, typename Compare = std::less<Key>>
using ArenaMap = std::map<Key, Value, Compare, memory::ArenaAllocator<std::pair<const Key, Value>>>;

} // namespace base::containers
