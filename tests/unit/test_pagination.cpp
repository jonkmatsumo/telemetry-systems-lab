#include <gtest/gtest.h>
#include "pagination.h"

TEST(PaginationTest, HasMoreUsesTotalWhenAvailable) {
    using telemetry::api::HasMore;
    EXPECT_TRUE(HasMore(20, 0, 20, 100));
    EXPECT_TRUE(HasMore(20, 40, 20, 100));
    EXPECT_FALSE(HasMore(20, 80, 20, 100));
}

TEST(PaginationTest, HasMoreFallsBackToLimitWhenTotalMissing) {
    using telemetry::api::HasMore;
    EXPECT_TRUE(HasMore(20, 0, 20, std::nullopt));
    EXPECT_FALSE(HasMore(20, 0, 10, std::nullopt));
}
