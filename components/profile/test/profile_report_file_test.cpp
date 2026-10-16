// Linked against a build of the profile library with BASE_PROFILE_REPORT_DIRECTORY and
// BASE_PROFILE_REPORT_NAME="run" set (see components/profile/CMakeLists.txt), because
// the report output is compiled into the library.
#include "base/profile/profile.hpp"

#include "exit_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

static_assert(BASE_ENABLE_PROFILING, "this test needs the zones enabled");

TEST(ProfileReportFileDeathTest, ReportGoesToTheConfiguredFileAtExit) {
    const std::filesystem::path reportDirectory = BASE_PROFILE_REPORT_DIRECTORY;
    std::filesystem::remove_all(reportDirectory);
    const std::filesystem::path capture = exit_test::fresh_scratch_directory("file-output") / "stdout.txt";

    EXPECT_EXIT(
        {
            exit_test::redirect_stdout_to(capture);
            {
                BASE_PROFILE_SCOPE("file.exit-report");
            }
            std::exit(0);
        },
        ::testing::ExitedWithCode(0),
        "");

    std::vector<std::filesystem::path> reports;
    if (std::filesystem::exists(reportDirectory)) {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(reportDirectory)) {
            reports.push_back(entry.path());
        }
    }
    ASSERT_EQ(reports.size(), 1U);
    const std::string fileName = reports.front().filename().string();
    EXPECT_TRUE(fileName.starts_with("run-")) << fileName;
    EXPECT_TRUE(fileName.ends_with("Z.txt")) << fileName;
    const std::string report = exit_test::read_file(reports.front());
    EXPECT_NE(report.find("file.exit-report"), std::string::npos) << report;

    const std::string output = exit_test::read_file(capture);
    EXPECT_NE(output.find("profile: report written to"), std::string::npos) << output;
    EXPECT_EQ(output.find("Total:"), std::string::npos) << output;

    std::filesystem::remove_all(reportDirectory);
    std::filesystem::remove_all(capture.parent_path());
}
