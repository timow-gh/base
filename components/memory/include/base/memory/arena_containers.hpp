#pragma once

#include "base/memory/arena_allocator.hpp"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace base::memory {

template <typename T>
using ArenaVector = std::vector<T, ArenaAllocator<T>>;

using ArenaString = std::basic_string<char, std::char_traits<char>, ArenaAllocator<char>>;

template <typename Key, typename Value, typename Compare = std::less<Key>>
using ArenaMap = std::map<Key, Value, Compare, ArenaAllocator<std::pair<const Key, Value>>>;

template <typename Key, typename Compare = std::less<Key>>
using ArenaSet = std::set<Key, Compare, ArenaAllocator<Key>>;

template <typename Key, typename Value, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
using ArenaUnorderedMap = std::unordered_map<Key, Value, Hash, KeyEqual, ArenaAllocator<std::pair<const Key, Value>>>;

template <typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
using ArenaUnorderedSet = std::unordered_set<Key, Hash, KeyEqual, ArenaAllocator<Key>>;

} // namespace base::memory
