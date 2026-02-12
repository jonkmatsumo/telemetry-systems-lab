#include <gtest/gtest.h>
#include <string>

#include "metrics.h"
#include "obs/metrics.h"

class ObsMetricsTest : public ::testing::Test {
protected:
    void SetUp() override {
        telemetry::metrics::MetricsRegistry::Instance().ResetForTesting();
    }

    void TearDown() override {
        telemetry::metrics::MetricsRegistry::Instance().ResetForTesting();
    }
};

TEST_F(ObsMetricsTest, EmitCounterUpdatesRegistry) {
    telemetry::obs::EmitCounter("test_obs_counter", 3, "count", "test");
    auto text = telemetry::metrics::MetricsRegistry::Instance().ToPrometheus();
    EXPECT_NE(text.find("test_obs_counter"), std::string::npos);
}

TEST_F(ObsMetricsTest, EmitHistogramUpdatesRegistry) {
    telemetry::obs::EmitHistogram("test_obs_latency_ms", 12.5, "ms", "test");
    auto text = telemetry::metrics::MetricsRegistry::Instance().ToPrometheus();
    EXPECT_NE(text.find("test_obs_latency_ms_count"), std::string::npos);
    EXPECT_NE(text.find("test_obs_latency_ms_sum"), std::string::npos);
}

TEST_F(ObsMetricsTest, SeriesCapDropsNewSeriesAndEmitsCounter) {
    auto& registry = telemetry::metrics::MetricsRegistry::Instance();
    registry.ConfigureSeriesCardinalityGuard(1);

    telemetry::obs::EmitCounter("cardinality_metric", 1, "count", "test", {{"label", "a"}});
    telemetry::obs::EmitCounter("cardinality_metric", 1, "count", "test", {{"label", "b"}});

    auto text = registry.ToPrometheus();
    EXPECT_NE(text.find("cardinality_metric{label=\"a\"} 1"), std::string::npos);
    EXPECT_EQ(text.find("cardinality_metric{label=\"b\"}"), std::string::npos);
    EXPECT_NE(text.find("metrics_series_dropped_total 1"), std::string::npos);
}

TEST_F(ObsMetricsTest, AllowlistBypassesCapButStillDropsNonAllowlistedSeries) {
    auto& registry = telemetry::metrics::MetricsRegistry::Instance();
    registry.ConfigureSeriesCardinalityGuard(1, {"allowlisted_metric"});

    telemetry::obs::EmitCounter("base_metric", 1, "count", "test", {{"label", "base"}});
    telemetry::obs::EmitCounter("allowlisted_metric", 1, "count", "test", {{"label", "x"}});
    telemetry::obs::EmitCounter("allowlisted_metric", 1, "count", "test", {{"label", "y"}});
    telemetry::obs::EmitCounter("blocked_metric", 1, "count", "test", {{"label", "blocked"}});

    auto text = registry.ToPrometheus();
    EXPECT_NE(text.find("allowlisted_metric{label=\"x\"} 1"), std::string::npos);
    EXPECT_NE(text.find("allowlisted_metric{label=\"y\"} 1"), std::string::npos);
    EXPECT_EQ(text.find("blocked_metric{label=\"blocked\"}"), std::string::npos);
    EXPECT_NE(text.find("metrics_series_dropped_total 1"), std::string::npos);
}
