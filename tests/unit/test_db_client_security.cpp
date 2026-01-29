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
