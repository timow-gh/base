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

TEST_F(AssertTest, PassingAssertionDoesNotCallHandler) {
    BASE_ASSERT(1 + 1 == 2, "arithmetic works");
    EXPECT_EQ(recorded().count, 0);
}

TEST_F(AssertTest, FailingAssertionCallsHandlerWithExpressionMessageAndLocation) {
#if BASE_ENABLE_ASSERTS
    const unsigned expectedLine = __LINE__ + 1;
    BASE_ASSERT(1 + 1 == 3, "arithmetic is broken");
    EXPECT_EQ(recorded().count, 1);
    EXPECT_EQ(recorded().expression, "1 + 1 == 3");
    EXPECT_EQ(recorded().message, "arithmetic is broken");
    EXPECT_EQ(recorded().line, expectedLine);
#else
    BASE_ASSERT(1 + 1 == 3, "compiled out");
    EXPECT_EQ(recorded().count, 0);
#endif
}

TEST_F(AssertTest, VerifyEvaluatesConditionInEveryBuild) {
    int evaluations = 0;
    BASE_VERIFY(++evaluations == 1, "side effect must happen");
    EXPECT_EQ(evaluations, 1);
    EXPECT_EQ(recorded().count, 0);
}

TEST_F(AssertTest, SetAssertHandlerReturnsPreviousHandler) {
    const base::AssertHandler current = base::set_assert_handler(nullptr);
    EXPECT_EQ(current, &recording_handler);
    // nullptr installed the default handler, which must not be nullptr itself.
    const base::AssertHandler defaultHandler = base::set_assert_handler(&recording_handler);
    EXPECT_NE(defaultHandler, nullptr);
}
