#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "api_debug.h"

TEST(ApiDebugTest, BuildDebugMetaBaseFields) {
    auto meta = BuildDebugMeta({12.5, 3});
    double duration = meta["duration_ms"].get<double>();
    long rows = meta["row_count"].get<long>();
    EXPECT_DOUBLE_EQ(duration, 12.5);
    EXPECT_EQ(rows, 3);
    EXPECT_FALSE(meta.contains("resolved"));
}

TEST(ApiDebugTest, BuildDebugMetaWithResolved) {
    nlohmann::json resolved;
    resolved["metrics"] = {"cpu_usage"};
    auto meta = BuildDebugMeta({1.0, 2, resolved});
    EXPECT_TRUE(meta.contains("resolved"));
    std::string metric = meta["resolved"]["metrics"][0].get<std::string>();
    EXPECT_EQ(metric, "cpu_usage");
}
