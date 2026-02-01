#include <gtest/gtest.h>
#include <string>

#include "metrics.h"
#include "obs/metrics.h"

TEST(ObsMetricsTest, EmitCounterUpdatesRegistry) {
    telemetry::obs::EmitCounter("test_obs_counter", 3, "count", "test");
    auto text = telemetry::metrics::MetricsRegistry::Instance().ToPrometheus();
    EXPECT_NE(text.find("test_obs_counter"), std::string::npos);
}

TEST(ObsMetricsTest, EmitHistogramUpdatesRegistry) {
    telemetry::obs::EmitHistogram("test_obs_latency_ms", 12.5, "ms", "test");
    auto text = telemetry::metrics::MetricsRegistry::Instance().ToPrometheus();
    EXPECT_NE(text.find("test_obs_latency_ms_count"), std::string::npos);
    EXPECT_NE(text.find("test_obs_latency_ms_sum"), std::string::npos);
}
