#pragma once

#include "base/core/config.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

// The report at exit is written by a static destructor, so the tests exit a child
// process with EXPECT_EXIT. EXPECT_EXIT only sees the child's stderr, so the child
// redirects stdout to a file that the parent reads afterwards.
namespace exit_test {

[[nodiscard]] inline std::filesystem::path scratch_directory(std::string_view testName) {
    return std::filesystem::temp_directory_path() / "base-profile-tests" / testName;
}

// Called in the parent before EXPECT_EXIT: an empty directory, so files left by an
// earlier run cannot satisfy the test.
[[nodiscard]] inline std::filesystem::path fresh_scratch_directory(std::string_view testName) {
    const std::filesystem::path directory = scratch_directory(testName);
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

// Called in the child process.
inline void redirect_stdout_to(const std::filesystem::path& file) {
    std::FILE* stream = nullptr;
#if BASE_OS_WINDOWS
    const bool redirected = _wfreopen_s(&stream, file.c_str(), L"w", stdout) == 0;
#else
    stream = std::freopen(file.c_str(), "w", stdout);
    const bool redirected = stream != nullptr;
#endif
    if (!redirected) {
        std::fputs("cannot redirect stdout\n", stderr);
        std::exit(1);
    }
}

[[nodiscard]] inline std::string read_file(const std::filesystem::path& file) {
    std::ifstream stream(file);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace exit_test
