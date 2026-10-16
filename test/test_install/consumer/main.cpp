#include <base/memory/arena.hpp>

#include <cstdio>
#include <optional>

int main() {
    std::optional<base::memory::Arena> arena = base::memory::Arena::create();
    if (!arena) {
        std::puts("Arena::create failed");
        return 1;
    }

    const int* value = arena->push<int>(42);
    if (value == nullptr || *value != 42) {
        std::puts("Arena::push failed");
        return 1;
    }

    std::puts("base::memory::Arena works");
    return 0;
}
