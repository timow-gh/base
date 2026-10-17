#pragma once

#include "base/memory/arena_allocator.hpp"

#include <functional>
#include <unordered_set>

namespace base::containers {

// A std::unordered_set whose nodes and buckets live in an Arena. Each rehash leaves
// the outgrown bucket array behind until the arena is rewound, so reserve() up front
// when the size is known.
// See base::memory::ArenaAllocator for the lifetime and assignment rules.
template <typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
using ArenaUnorderedSet = std::unordered_set<Key, Hash, KeyEqual, memory::ArenaAllocator<Key>>;

} // namespace base::containers
