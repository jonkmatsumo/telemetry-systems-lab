#include <gtest/gtest.h>
#include "contract.h"

using namespace telemetry::anomaly;

// Test contract ordering
TEST(FeatureContractTest, VectorMappingIsCorrect) {
    TelemetryRecord r;
    r.cpu_usage = 10.0;
    r.memory_usage = 20.0;
    r.disk_utilization = 30.0;
    r.network_rx_rate = 40.0;
    r.network_tx_rate = 50.0;

    FeatureVector v = FeatureVector::FromRecord(r);

    EXPECT_DOUBLE_EQ(v.data[0], 10.0);
    EXPECT_DOUBLE_EQ(v.data[1], 20.0);
    EXPECT_DOUBLE_EQ(v.data[2], 30.0);
    EXPECT_DOUBLE_EQ(v.data[3], 40.0);
    EXPECT_DOUBLE_EQ(v.data[4], 50.0);

    // Verify accessors match indices
    EXPECT_EQ(&v.cpu_usage(), &v.data[0]);
    EXPECT_EQ(&v.memory_usage(), &v.data[1]);
    EXPECT_EQ(&v.disk_utilization(), &v.data[2]);
    EXPECT_EQ(&v.network_rx_rate(), &v.data[3]);
    EXPECT_EQ(&v.network_tx_rate(), &v.data[4]);
}

TEST(FeatureContractTest, MetadataNamesMatchSize) {
    auto names = FeatureMetadata::GetFeatureNames();
    EXPECT_EQ(names.size(), FeatureVector::kSize);
    EXPECT_EQ(names[0], "cpu_usage");
    EXPECT_EQ(names[3], "network_rx_rate");
}
