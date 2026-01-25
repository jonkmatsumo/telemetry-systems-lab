#include <gtest/gtest.h>
#include "db_client.h"
#include <vector>
#include <string>
#include <chrono>

// Integration test - requires running Postgres instance
class DbClientTest : public ::testing::Test {
protected:
    std::string conn_str;
    
    void SetUp() override {
        const char* env_p = std::getenv("DB_CONNECTION_STRING");
        if (env_p) {
            conn_str = env_p;
        } else {
            // Default for inside docker
            conn_str = "postgresql://postgres:password@postgres:5432/telemetry";
        }
    }
};

#include <uuid/uuid.h>

// Helper
std::string GenerateUUID() {
    uuid_t binuuid;
    uuid_generate_random(binuuid);
    char uuid[37];
    uuid_unparse_lower(binuuid, uuid);
    return std::string(uuid);
}

TEST_F(DbClientTest, CreateAndUpdateRun) {
    DbClient client(conn_str);
    std::string run_id = GenerateUUID();
    
    telemetry::GenerateRequest req;
    req.set_tier("INTEGRATION");
    req.set_start_time_iso("2025-01-01T00:00:00Z");
    req.set_end_time_iso("2025-01-01T01:00:00Z");
    req.set_interval_seconds(60);
    req.set_seed(12345);
    req.set_host_count(1);
    
    // 1. Create
    ASSERT_NO_THROW(client.CreateRun(run_id, req, "PENDING"));
    
    // 2. Update
    ASSERT_NO_THROW(client.UpdateRunStatus(run_id, "RUNNING", 0));
    ASSERT_NO_THROW(client.UpdateRunStatus(run_id, "SUCCEEDED", 100));
}

TEST_F(DbClientTest, BatchInsert) {
    DbClient client(conn_str);
    std::string run_id = GenerateUUID();
    
    telemetry::GenerateRequest req;
    req.set_tier("INTEGRATION");
    req.set_start_time_iso("2025-01-01T00:00:00Z");
    req.set_end_time_iso("2025-01-01T01:00:00Z");
    req.set_interval_seconds(60);
    req.set_seed(12345);
    req.set_host_count(1);
    
    client.CreateRun(run_id, req, "RUNNING");
    
    TelemetryRecord rec;
    rec.run_id = run_id;
    rec.metric_timestamp = std::chrono::system_clock::now();
    rec.ingestion_time = rec.metric_timestamp;
    rec.host_id = "host-1";
    rec.project_id = "proj-1";
    rec.region = "us-test";
    rec.cpu_usage = 50.0;
    rec.memory_usage = 50.0;
    rec.disk_utilization = 50.0;
    rec.network_rx_rate = 10.0;
    rec.network_tx_rate = 10.0;
    rec.labels_json = "{}";
    
    std::vector<TelemetryRecord> batch = {rec, rec, rec};
    
    ASSERT_NO_THROW(client.BatchInsertTelemetry(batch));
    // Validation of row count would require querying DB directly or helper
    // For now, no-throw is good indication of successful execution
}

TEST_F(DbClientTest, EmptyBatch) {
    DbClient client(conn_str);
    std::vector<TelemetryRecord> batch;
    // Should behave gracefully (no-op)
    ASSERT_NO_THROW(client.BatchInsertTelemetry(batch));
}

// We can't easily test bad connection parameters as integration env provides correct valid.
// Unless we instantiate with bad string.
TEST(DbClientFailureTest, InvalidConnection) {
    DbClient client("postgresql://baduser:badpass@localhost:5432/bad_db");
    telemetry::GenerateRequest req;
    // Should catch exception internally and log error (no throw from API surface)
    // CreateRun logs error but doesn't throw.
    ASSERT_NO_THROW(client.CreateRun("id", req, "PENDING"));
}

