#pragma once

#include "base/core/config.hpp"

// Manual AddressSanitizer poisoning for memory that a custom allocator owns but
// has not handed out. Accesses to poisoned memory are reported by ASan like a
// use-after-free. Without ASan the macros compile to nothing.
#if BASE_HAS_ASAN
#include <sanitizer/asan_interface.h>

#define BASE_ASAN_POISON(address, size) ASAN_POISON_MEMORY_REGION((address), (size))
#define BASE_ASAN_UNPOISON(address, size) ASAN_UNPOISON_MEMORY_REGION((address), (size))
#else
#define BASE_ASAN_POISON(address, size)                                                                                \
    do {                                                                                                               \
        static_cast<void>(address);                                                                                    \
        static_cast<void>(size);                                                                                       \
    } while (false)
#define BASE_ASAN_UNPOISON(address, size) BASE_ASAN_POISON(address, size)
#endif
