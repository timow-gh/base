#include "base/core/assert.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <print>

namespace base {

namespace detail {

namespace {

void default_assert_handler(std::string_view expression,
                            std::string_view message,
                            const std::source_location& location) {
    std::println(stderr,
                 "{}:{}: assertion failed: {}\n  in {}\n  {}",
                 location.file_name(),
                 location.line(),
                 expression,
                 location.function_name(),
                 message);
    std::fflush(stderr);
    std::abort();
}

// Atomic because assertions can fire on any thread while a test swaps the handler.
std::atomic<AssertHandler>& assert_handler() noexcept {
    static std::atomic<AssertHandler> handler{&default_assert_handler};
    return handler;
}

} // namespace

void assert_failed(std::string_view expression,
                   std::string_view message,
                   const std::source_location& location) noexcept {
    const AssertHandler handler = assert_handler().load(std::memory_order_acquire);
    handler(expression, message, location);
}

} // namespace detail

AssertHandler set_assert_handler(AssertHandler handler) noexcept {
    if (handler == nullptr) {
        handler = &detail::default_assert_handler;
    }
    return detail::assert_handler().exchange(handler, std::memory_order_acq_rel);
}

} // namespace base
