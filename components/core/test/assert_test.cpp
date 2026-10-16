#include "base/core/assert.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

struct RecordedFailure {
    int count = 0;
    std::string expression;
    std::string message;
    unsigned line = 0;
};

RecordedFailure& recorded() {
    static RecordedFailure failure;
    return failure;
}

void recording_handler(std::string_view expression, std::string_view message, const std::source_location& location) {
    RecordedFailure& failure = recorded();
    ++failure.count;
    failure.expression = expression;
    failure.message = message;
    failure.line = static_cast<unsigned>(location.line());
}

class AssertTest : public ::testing::Test {
protected:
    void SetUp() override {
        recorded() = {};
        m_previous = base::set_assert_handler(&recording_handler);
    }

    void TearDown() override { base::set_assert_handler(m_previous); }

private:
    base::AssertHandler m_previous = nullptr;
};

} // namespace

TEST_F(AssertTest, PassingAssertionsDoNotCallHandler) {
    BASE_ASSERT(1 + 1 == 2);
    BASE_ASSERT_MSG(1 + 1 == 2, "arithmetic works");
    EXPECT_EQ(recorded().count, 0);
}

TEST_F(AssertTest, FailingAssertReportsExpressionAndEmptyMessage) {
#if BASE_ENABLE_ASSERTS
    const unsigned expectedLine = __LINE__ + 1;
    BASE_ASSERT(1 + 1 == 3);
    EXPECT_EQ(recorded().count, 1);
    EXPECT_EQ(recorded().expression, "1 + 1 == 3");
    EXPECT_TRUE(recorded().message.empty());
    EXPECT_EQ(recorded().line, expectedLine);
#else
    GTEST_SKIP() << "assertions are disabled";
#endif
}

TEST_F(AssertTest, FailingAssertMsgReportsExpressionMessageAndLocation) {
#if BASE_ENABLE_ASSERTS
    const unsigned expectedLine = __LINE__ + 1;
    BASE_ASSERT_MSG(1 + 1 == 3, "arithmetic is broken");
    EXPECT_EQ(recorded().count, 1);
    EXPECT_EQ(recorded().expression, "1 + 1 == 3");
    EXPECT_EQ(recorded().message, "arithmetic is broken");
    EXPECT_EQ(recorded().line, expectedLine);
#else
    GTEST_SKIP() << "assertions are disabled";
#endif
}

// Disabled assertions must cost nothing at runtime: the condition, including its
// side effects, is not evaluated. Clang rightly warns about side effects in an
// unevaluated operand; here they are the point of the test.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunevaluated-expression"
#endif
TEST_F(AssertTest, DisabledAssertionsDoNotEvaluateTheCondition) {
    int evaluations = 0;
    BASE_ASSERT(++evaluations > 0);
    BASE_ASSERT_MSG(++evaluations > 0, "counted");
#if BASE_ENABLE_ASSERTS
    EXPECT_EQ(evaluations, 2);
#else
    EXPECT_EQ(evaluations, 0);
#endif
    EXPECT_EQ(recorded().count, 0);
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

TEST_F(AssertTest, SetAssertHandlerReturnsPreviousHandler) {
    const base::AssertHandler current = base::set_assert_handler(nullptr);
    EXPECT_EQ(current, &recording_handler);
    // nullptr installed the default handler, which must not be nullptr itself.
    const base::AssertHandler defaultHandler = base::set_assert_handler(&recording_handler);
    EXPECT_NE(defaultHandler, nullptr);
}
