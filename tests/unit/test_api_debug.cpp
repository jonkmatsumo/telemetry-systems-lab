#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "api_debug.h"

TEST(ApiDebugTest, BuildDebugMetaBaseFields) {
    auto meta = BuildDebugMeta({12.5, 3});
    EXPECT_DOUBLE_EQ(meta["duration_ms"].get<double>(), 12.5);
    EXPECT_EQ(meta["row_count"].get<long>(), 3);
    EXPECT_FALSE(meta.contains("resolved"));
}

TEST(ApiDebugTest, BuildDebugMetaWithResolved) {
    nlohmann::json resolved;
    resolved["metrics"] = {"cpu_usage"};
    auto meta = BuildDebugMeta({1.0, 2, resolved});
    EXPECT_TRUE(meta.contains("resolved"));
    EXPECT_EQ(meta["resolved"]["metrics"][0].get<std::string>(), "cpu_usage");
}
