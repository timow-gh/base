#pragma once

#include "base/core/config.hpp"

#include <source_location>
#include <string_view>

namespace base {

/**
 * \brief Receives every failed assertion.
 *
 * The default handler prints the failure to stderr and aborts. A custom handler may
 * return, in which case execution continues after the failed assertion; that is only
 * meant for tests that check that an assertion fires.
 */
using AssertHandler = void (*)(std::string_view expression,
                               std::string_view message,
                               const std::source_location& location);

/**
 * \brief Installs `handler` for all subsequent assertion failures.
 * \param handler The new handler, or nullptr to restore the default handler.
 * \return The previously installed handler.
 */
AssertHandler set_assert_handler(AssertHandler handler) noexcept;

namespace detail {

// Out of line so that the assertion macro costs one compare and a cold call.
void assert_failed(std::string_view expression,
                   std::string_view message,
                   const std::source_location& location = std::source_location::current()) noexcept;

} // namespace detail

} // namespace base

// The disabled forms keep `condition` in an unevaluated sizeof, so variables that
// are only used in assertions don't trigger unused warnings in release builds.
#if BASE_ENABLE_ASSERTS
/**
 * \brief Checks a pre- or postcondition in builds with BASE_ENABLE_ASSERTS.
 */
#define BASE_ASSERT(condition, message)                                                                                \
    ((condition) ? static_cast<void>(0) : ::base::detail::assert_failed(#condition, message))
#else
#define BASE_ASSERT(condition, message) static_cast<void>(sizeof((condition) ? 1 : 0))
#endif

#if BASE_ENABLE_ASSERTS
/**
 * \brief Like BASE_ASSERT, but `condition` is evaluated in every build. Use it when
 * the expression has a side effect that must happen.
 */
#define BASE_VERIFY(condition, message) BASE_ASSERT(condition, message)
#else
#define BASE_VERIFY(condition, message) static_cast<void>(condition)
#endif
