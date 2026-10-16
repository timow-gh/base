#pragma once

// Every macro below is always defined, as 0 or 1, so that it can be tested with
// `#if` and a misspelled name is caught by -Wundef instead of silently being 0.
// They must stay macros (not enums) to be usable in preprocessor conditions, and
// `#elif defined(...)` is kept over C++23's `#elifdef` for older preprocessors.
// NOLINTBEGIN(modernize-macro-to-enum, readability-use-concise-preprocessor-directives)

#if defined(_WIN32)
#define BASE_OS_WINDOWS 1
#define BASE_OS_LINUX 0
#define BASE_OS_MACOS 0
#elif defined(__APPLE__)
#define BASE_OS_WINDOWS 0
#define BASE_OS_LINUX 0
#define BASE_OS_MACOS 1
#elif defined(__linux__)
#define BASE_OS_WINDOWS 0
#define BASE_OS_LINUX 1
#define BASE_OS_MACOS 0
#else
#error "base: unsupported operating system"
#endif

// clang-cl defines _MSC_VER as well, so Clang is detected first.
#if defined(__clang__)
#define BASE_COMPILER_CLANG 1
#define BASE_COMPILER_MSVC 0
#define BASE_COMPILER_GCC 0
#elif defined(_MSC_VER)
#define BASE_COMPILER_CLANG 0
#define BASE_COMPILER_MSVC 1
#define BASE_COMPILER_GCC 0
#elif defined(__GNUC__)
#define BASE_COMPILER_CLANG 0
#define BASE_COMPILER_MSVC 0
#define BASE_COMPILER_GCC 1
#else
#error "base: unsupported compiler"
#endif

#if defined(_M_X64) || defined(__x86_64__)
#define BASE_ARCH_X64 1
#define BASE_ARCH_ARM64 0
#elif defined(_M_ARM64) || defined(__aarch64__)
#define BASE_ARCH_X64 0
#define BASE_ARCH_ARM64 1
#else
#error "base: unsupported architecture, only 64-bit x64 and ARM64 are supported"
#endif

// GCC and MSVC (/fsanitize=address) define __SANITIZE_ADDRESS__, Clang reports it
// through __has_feature.
#if defined(__SANITIZE_ADDRESS__)
#define BASE_HAS_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define BASE_HAS_ASAN 1
#else
#define BASE_HAS_ASAN 0
#endif
#else
#define BASE_HAS_ASAN 0
#endif

// Assertions follow NDEBUG by default (on in Debug, off in Release builds). Define
// BASE_ENABLE_ASSERTS to 0 or 1 to override it.
#ifndef BASE_ENABLE_ASSERTS
#ifdef NDEBUG
#define BASE_ENABLE_ASSERTS 0
#else
#define BASE_ENABLE_ASSERTS 1
#endif
#endif

// NOLINTEND(modernize-macro-to-enum, readability-use-concise-preprocessor-directives)
