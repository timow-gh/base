#include "base/base.hpp"

#include <fmt/core.h>

namespace base {

void libraryFunction(int value) {
    fmt::print("Hello from libraryFunction! The answer is {}\n", value);
}

} // namespace base
