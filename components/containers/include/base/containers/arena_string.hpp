#pragma once

#include "base/memory/arena_allocator.hpp"

#include <string>

namespace base::containers {

// A std::string in an Arena; short strings stay in the small buffer and never touch it.
// See base::memory::ArenaAllocator for the lifetime and assignment rules.
using ArenaString = std::basic_string<char, std::char_traits<char>, memory::ArenaAllocator<char>>;

} // namespace base::containers
