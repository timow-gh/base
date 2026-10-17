#include "base/memory/arena_allocator.hpp"

#include <new>

namespace base::memory::detail {

void throw_bad_alloc() {
    throw std::bad_alloc();
}

void throw_bad_array_new_length() {
    throw std::bad_array_new_length();
}

} // namespace base::memory::detail
