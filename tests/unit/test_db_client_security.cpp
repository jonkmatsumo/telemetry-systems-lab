#include <gtest/gtest.h>
#include "db_client.h"

// Test that DbClient::IsValidMetric correctly validates metrics against
// the allowlist and rejects SQL injection attempts.

TEST(DbClientSecurityTest, AcceptsValidMetrics) {
    // These are the actual column names in host_telemetry_archival
    EXPECT_TRUE(DbClient::IsValidMetric("cpu_usage"));
    EXPECT_TRUE(DbClient::IsValidMetric("memory_usage"));
    EXPECT_TRUE(DbClient::IsValidMetric("disk_utilization"));
    EXPECT_TRUE(DbClient::IsValidMetric("network_rx_rate"));
    EXPECT_TRUE(DbClient::IsValidMetric("network_tx_rate"));
}

TEST(DbClientSecurityTest, RejectsInvalidMetrics) {
    // Unknown column names
    EXPECT_FALSE(DbClient::IsValidMetric("invalid_column"));
    EXPECT_FALSE(DbClient::IsValidMetric("foo"));
    EXPECT_FALSE(DbClient::IsValidMetric("CPU_USAGE"));  // case sensitive

    // Empty string
    EXPECT_FALSE(DbClient::IsValidMetric(""));
}

TEST(DbClientSecurityTest, RejectsSqlInjectionAttempts) {
    // SQL injection via identifier manipulation
    EXPECT_FALSE(DbClient::IsValidMetric("cpu_usage; DROP TABLE users;"));
    EXPECT_FALSE(DbClient::IsValidMetric("cpu_usage) FROM host_telemetry_archival; --"));
    EXPECT_FALSE(DbClient::IsValidMetric("1; DELETE FROM host_telemetry_archival; --"));
    EXPECT_FALSE(DbClient::IsValidMetric("cpu_usage, password"));
    EXPECT_FALSE(DbClient::IsValidMetric("* FROM users --"));

    // Whitespace/control character attacks
    EXPECT_FALSE(DbClient::IsValidMetric("cpu_usage\n; DROP TABLE"));
    EXPECT_FALSE(DbClient::IsValidMetric("cpu_usage\t"));
    EXPECT_FALSE(DbClient::IsValidMetric(" cpu_usage"));
    EXPECT_FALSE(DbClient::IsValidMetric("cpu_usage "));
}

TEST(DbClientSecurityTest, AcceptsValidDimensions) {
    EXPECT_TRUE(DbClient::IsValidDimension("region"));
    EXPECT_TRUE(DbClient::IsValidDimension("project_id"));
    EXPECT_TRUE(DbClient::IsValidDimension("host_id"));
    EXPECT_TRUE(DbClient::IsValidDimension("anomaly_type"));
    EXPECT_TRUE(DbClient::IsValidDimension("h.region"));
}

TEST(DbClientSecurityTest, RejectsInvalidDimensions) {
    EXPECT_FALSE(DbClient::IsValidDimension("invalid_column"));
    EXPECT_FALSE(DbClient::IsValidDimension("password"));
    EXPECT_FALSE(DbClient::IsValidDimension("region; DROP TABLE users;"));
}

TEST(DbClientSecurityTest, AcceptsValidAggregations) {
    EXPECT_TRUE(DbClient::IsValidAggregation("mean"));
    EXPECT_TRUE(DbClient::IsValidAggregation("min"));
    EXPECT_TRUE(DbClient::IsValidAggregation("max"));
    EXPECT_TRUE(DbClient::IsValidAggregation("p50"));
    EXPECT_TRUE(DbClient::IsValidAggregation("p95"));
}

TEST(DbClientSecurityTest, RejectsInvalidAggregations) {
    EXPECT_FALSE(DbClient::IsValidAggregation("stddev"));
    EXPECT_FALSE(DbClient::IsValidAggregation("sum")); // Not in current allowlist
    EXPECT_FALSE(DbClient::IsValidAggregation("mean; DROP TABLE users;"));
    EXPECT_FALSE(DbClient::IsValidAggregation(""));
}
