// Disabled regardless of base_PROFILING. Only the macros depend on the switch, so the
// enabled and disabled test files can share one binary.
#undef BASE_ENABLE_PROFILING
#define BASE_ENABLE_PROFILING 0

#include "base/profile/profile.hpp"

#include <gtest/gtest.h>

// Disabled zones must cost nothing at runtime: the byte count, including its side
// effects, is not evaluated. Clang rightly warns about side effects in an unevaluated
// operand; here they are the point of the test.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunevaluated-expression"
#endif
TEST(ProfileDisabled, ZonesRecordNothingAndDoNotEvaluateTheByteCount) {
    int evaluations = 0;
    {
        BASE_PROFILE_FUNCTION();
        BASE_PROFILE_SCOPE("disabled.scope");
        BASE_PROFILE_BANDWIDTH("disabled.bandwidth", ++evaluations);
    }
    EXPECT_EQ(evaluations, 0);
    EXPECT_FALSE(base::profile::zone_stats("disabled.scope").has_value());
    EXPECT_FALSE(base::profile::zone_stats("disabled.bandwidth").has_value());
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
