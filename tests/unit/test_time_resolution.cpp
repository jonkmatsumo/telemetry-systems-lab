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
