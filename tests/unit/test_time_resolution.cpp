#include <gtest/gtest.h>
#include "time_resolution.h"

TEST(TimeResolutionTest, SelectBucketSecondsMapsRanges) {
    using telemetry::api::SelectBucketSeconds;
    EXPECT_EQ(SelectBucketSeconds("2026-02-03T00:00:00Z", "2026-02-03T05:59:59Z"), 300);
    EXPECT_EQ(SelectBucketSeconds("2026-02-03T00:00:00Z", "2026-02-04T00:00:00Z"), 3600);
    EXPECT_EQ(SelectBucketSeconds("2026-02-01T00:00:00Z", "2026-02-20T00:00:00Z"), 21600);
    EXPECT_EQ(SelectBucketSeconds("2026-01-01T00:00:00Z", "2026-05-01T00:00:00Z"), 86400);
    EXPECT_EQ(SelectBucketSeconds("2025-01-01T00:00:00Z", "2026-02-03T00:00:00Z"), 604800);
}

TEST(TimeResolutionTest, PreviousPeriodWindowComputesPriorRange) {
    using telemetry::api::PreviousPeriodWindow;
    auto window = PreviousPeriodWindow("2026-02-03T00:00:00Z", "2026-02-04T00:00:00Z");
    ASSERT_TRUE(window.has_value());
    EXPECT_EQ(window->first, "2026-02-02T00:00:00Z");
    EXPECT_EQ(window->second, "2026-02-03T00:00:00Z");
}

TEST(TimeResolutionTest, PreviousPeriodWindowRejectsInvalidRange) {
    using telemetry::api::PreviousPeriodWindow;
    EXPECT_FALSE(PreviousPeriodWindow("", "2026-02-04T00:00:00Z").has_value());
    EXPECT_FALSE(PreviousPeriodWindow("2026-02-04T00:00:00Z", "2026-02-03T00:00:00Z").has_value());
}
