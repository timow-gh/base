#pragma once

#include "base/memory/arena_allocator.hpp"

#include <functional>
#include <unordered_map>
#include <utility>

namespace base::containers {

// A std::unordered_map whose nodes and buckets live in an Arena. Each rehash leaves
// the outgrown bucket array behind until the arena is rewound, so reserve() up front
// when the size is known.
// See base::memory::ArenaAllocator for the lifetime and assignment rules.
template <typename Key, typename Value, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
using ArenaUnorderedMap =
    std::unordered_map<Key, Value, Hash, KeyEqual, memory::ArenaAllocator<std::pair<const Key, Value>>>;

} // namespace base::containers
