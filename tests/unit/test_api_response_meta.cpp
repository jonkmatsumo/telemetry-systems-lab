#include <gtest/gtest.h>
#include "api_response_meta.h"

using telemetry::api::BuildResponseMeta;
using telemetry::api::IsTruncated;

TEST(ApiResponseMetaTest, TruncationDependsOnTotalDistinct) {
    EXPECT_TRUE(IsTruncated(10, 10, 12));
    EXPECT_FALSE(IsTruncated(10, 10, 10));
    EXPECT_FALSE(IsTruncated(10, 10, std::nullopt));
}

TEST(ApiResponseMetaTest, BuildResponseMetaUsesNullTotalDistinct) {
    auto meta = BuildResponseMeta({10, 5, false, std::nullopt, "top_k_limit"});
    EXPECT_EQ(meta["limit"].get<int>(), 10);
    EXPECT_EQ(meta["returned"].get<int>(), 5);
    EXPECT_FALSE(meta["truncated"].get<bool>());
    EXPECT_TRUE(meta["total_distinct"].is_null());
    std::string reason = meta["reason"].get<std::string>();
    EXPECT_EQ(reason, "top_k_limit");
}

TEST(ApiResponseMetaTest, BuildResponseMetaIncludesBinsInfo) {
    auto meta = BuildResponseMeta({500, 50, true, std::nullopt, "max_bins_cap", 500, 50});
    EXPECT_EQ(meta["limit"].get<int>(), 500);
    EXPECT_EQ(meta["returned"].get<int>(), 50);
    EXPECT_TRUE(meta["truncated"].get<bool>());
    EXPECT_EQ(meta["bins_requested"].get<int>(), 500);
    EXPECT_EQ(meta["bins_returned"].get<int>(), 50);
}
