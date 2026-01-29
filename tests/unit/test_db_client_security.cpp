#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "db_client.h"

// We can mock the DB connection or just test the logic if we refactor validation out.
// However, since DbClient is monolithic, we might need a real DB or a mock.
// Given the instructions, we can rely on "Add unit/integration coverage".
// A unit test that doesn't hit the DB but tests validation logic would be best,
// but validation is inside GetHistogram which hits DB.
// So let's add a test that expects an exception or empty result for invalid metric.
// Wait, we can't easily mock pqxx here without refactoring DbClient to take a connector.
// So we will assume we can rely on integration test or just test the API behavior?
// The prompt says "Prefer adding a focused unit test if possible".
// I will create a test that uses a real DB connection if available, or just checks if I can expose the validation logic.

// Actually, I can check if I can refactor validation to a static helper method in DbClient
// and test that helper. That's the cleanest way without a DB.

#include "../../src/db_client.h"

// I will add a friend declaration or just make validation public/static.
// For now, let's write the test assuming I'll add a static IsValidMetric method.

TEST(DbClientSecurityTest, MetricValidation) {
    EXPECT_TRUE(DbClient::IsValidMetric("cpu_usage"));
    EXPECT_TRUE(DbClient::IsValidMetric("memory_usage"));
    EXPECT_TRUE(DbClient::IsValidMetric("disk_utilization"));
    EXPECT_TRUE(DbClient::IsValidMetric("network_rx_rate"));
    EXPECT_TRUE(DbClient::IsValidMetric("network_tx_rate"));
    
    EXPECT_FALSE(DbClient::IsValidMetric("cpu_usage; DROP TABLE users;"));
    EXPECT_FALSE(DbClient::IsValidMetric("invalid_column"));
    EXPECT_FALSE(DbClient::IsValidMetric(""));
}
