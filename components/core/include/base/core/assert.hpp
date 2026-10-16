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
 *
 * \param message Empty for the condition-only macros (BASE_ASSERT, BASE_VERIFY).
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

// Separate macros with and without a message instead of one macro with an optional
// argument: the portable optional-argument tricks need __VA_OPT__, which MSVC only
// supports with /Zc:preprocessor, and these macros expand in every consumer's code.

#if BASE_ENABLE_ASSERTS
/**
 * \brief Checks a pre- or postcondition in builds with BASE_ENABLE_ASSERTS.
 */
#define BASE_ASSERT(condition) BASE_ASSERT_MSG(condition, "")

/**
 * \brief Like BASE_ASSERT, with a message that explains the failure.
 */
#define BASE_ASSERT_MSG(condition, message)                                                                            \
    ((condition) ? static_cast<void>(0) : ::base::detail::assert_failed(#condition, message))
#else
// The operand of sizeof is never evaluated: the condition is type-checked but no
// code is generated, not even at -O0, and function calls or side effects in it never
// happen. Keeping it in sizeof (instead of expanding to nothing) means variables
// that are only used in assertions are not reported as unused, and an assertion
// that stopped compiling is noticed in release builds too.
#define BASE_ASSERT(condition) static_cast<void>(sizeof((condition) ? 1 : 0))
#define BASE_ASSERT_MSG(condition, message) static_cast<void>(sizeof((condition) ? 1 : 0))
#endif

#if BASE_ENABLE_ASSERTS
/**
 * \brief Like BASE_ASSERT, but `condition` is evaluated in every build. Use it when
 * the expression has a side effect that must happen.
 */
#define BASE_VERIFY(condition) BASE_ASSERT_MSG(condition, "")

/**
 * \brief Like BASE_VERIFY, with a message that explains the failure.
 */
#define BASE_VERIFY_MSG(condition, message) BASE_ASSERT_MSG(condition, message)
#else
#define BASE_VERIFY(condition) static_cast<void>(condition)
#define BASE_VERIFY_MSG(condition, message) static_cast<void>(condition)
#endif
