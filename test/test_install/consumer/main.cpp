#include <base/containers/arena_vector.hpp>
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

    base::containers::ArenaVector<int> values(*arena);
    values.assign({1, 2, 3});
    if (values.back() != 3 || &values.get_allocator().arena() != &*arena) {
        std::puts("ArenaVector failed");
        return 1;
    }

    std::puts("base::memory::Arena and base::containers::ArenaVector work");
    return 0;
}
