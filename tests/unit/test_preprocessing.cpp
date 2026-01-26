#include <gtest/gtest.h>
#include "preprocessing.h"

using namespace telemetry::anomaly;

TEST(PreprocessingTest, ClampsNeedToZero) {
    PreprocessingConfig config;
    Preprocessor p(config);

    FeatureVector v;
    v.cpu_usage() = -5.0;     // Should be clamped
    v.memory_usage() = 10.0;
    v.disk_utilization() = -0.001; // Should be clamped
    v.network_rx_rate() = -100.0;  // Should be clamped
    v.network_tx_rate() = 50.0;

    p.Apply(v);

    EXPECT_DOUBLE_EQ(v.cpu_usage(), 0.0);
    EXPECT_DOUBLE_EQ(v.memory_usage(), 10.0);
    EXPECT_DOUBLE_EQ(v.disk_utilization(), 0.0);
    EXPECT_DOUBLE_EQ(v.network_rx_rate(), 0.0);
    EXPECT_DOUBLE_EQ(v.network_tx_rate(), 50.0);
}

TEST(PreprocessingTest, Log1pOptionWorks) {
    PreprocessingConfig config;
    config.log1p_network = true;
    Preprocessor p(config);

    FeatureVector v;
    v.cpu_usage() = 10.0;
    v.network_rx_rate() = 100.0;
    v.network_tx_rate() = 0.0;

    p.Apply(v);

    EXPECT_DOUBLE_EQ(v.cpu_usage(), 10.0); // Not affected
    EXPECT_DOUBLE_EQ(v.network_rx_rate(), std::log1p(100.0));
    EXPECT_DOUBLE_EQ(v.network_tx_rate(), std::log1p(0.0)); // 0 -> 0
}
