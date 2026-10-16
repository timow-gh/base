#include <base/core/bits.hpp>

#include <cstddef>

int main() {
    return base::align_up(std::size_t{5}, std::size_t{8}) == 8U ? 0 : 1;
}
