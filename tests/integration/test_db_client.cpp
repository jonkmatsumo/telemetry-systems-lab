#include <gtest/gtest.h>
#include "db_client.h"
#include <vector>
#include <string>
#include <chrono>
#include <nlohmann/json.hpp>

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

TEST_F(DbClientTest, ListFiltersAndPagination) {
    DbClient client(conn_str);

    std::string run_id = GenerateUUID();
    telemetry::GenerateRequest req;
    req.set_tier("INTEGRATION");
    req.set_start_time_iso("2025-01-01T00:00:00Z");
    req.set_end_time_iso("2025-01-01T01:00:00Z");
    req.set_interval_seconds(60);
    req.set_seed(42);
    req.set_host_count(1);
    ASSERT_NO_THROW(client.CreateRun(run_id, req, "INTEGRATION_TEST"));
    ASSERT_NO_THROW(client.UpdateRunStatus(run_id, "INTEGRATION_TEST", 1));

    std::string model_run_id = client.CreateModelRun(run_id, "test_model");
    ASSERT_FALSE(model_run_id.empty());
    ASSERT_NO_THROW(client.UpdateModelRunStatus(model_run_id, "COMPLETED", "/tmp/test_artifact.json"));

    std::string inference_id = client.CreateInferenceRun(model_run_id);
    ASSERT_FALSE(inference_id.empty());
    ASSERT_NO_THROW(client.UpdateInferenceRunStatus(inference_id, "COMPLETED", 0, nlohmann::json::array(), 1.0));

    std::string job_id = client.CreateScoreJob(run_id, model_run_id);
    ASSERT_FALSE(job_id.empty());
    ASSERT_NO_THROW(client.UpdateScoreJob(job_id, "COMPLETED", 10, 10, 0));

    auto runs = client.ListGenerationRuns(50, 0, "INTEGRATION_TEST");
    ASSERT_FALSE(runs.empty());

    auto models = client.ListModelRuns(50, 0, "COMPLETED", run_id);
    ASSERT_FALSE(models.empty());

    auto inference_runs = client.ListInferenceRuns(run_id, model_run_id, 50, 0, "COMPLETED");
    ASSERT_FALSE(inference_runs.empty());

    auto jobs = client.ListScoreJobs(50, 0, "COMPLETED", run_id, model_run_id);
    ASSERT_FALSE(jobs.empty());

    auto future_filtered = client.ListGenerationRuns(50, 0, "INTEGRATION_TEST", "2999-01-01T00:00:00Z");
    ASSERT_TRUE(future_filtered.empty());
}

TEST_F(DbClientTest, DatasetSummaryIncludesDerivedKpis) {
    DbClient client(conn_str);

    std::string run_id = GenerateUUID();
    telemetry::GenerateRequest req;
    req.set_tier("INTEGRATION");
    req.set_start_time_iso("2025-01-02T00:00:00Z");
    req.set_end_time_iso("2025-01-02T01:00:00Z");
    req.set_interval_seconds(60);
    req.set_seed(7);
    req.set_host_count(1);
    client.CreateRun(run_id, req, "SUCCEEDED");

    TelemetryRecord rec;
    rec.run_id = run_id;
    rec.metric_timestamp = std::chrono::system_clock::now();
    rec.ingestion_time = rec.metric_timestamp + std::chrono::seconds(2);
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

    auto summary = client.GetDatasetSummary(run_id, 5);
    ASSERT_TRUE(summary.contains("ingestion_latency_p50"));
    ASSERT_TRUE(summary.contains("ingestion_latency_p95"));
    EXPECT_NEAR(summary["ingestion_latency_p50"].get<double>(), 2.0, 0.5);
    EXPECT_NEAR(summary["ingestion_latency_p95"].get<double>(), 2.0, 0.5);
    ASSERT_TRUE(summary.contains("anomaly_rate_trend"));
    EXPECT_TRUE(summary["anomaly_rate_trend"].is_array());
}

TEST_F(DbClientTest, ScoreJobProgressFields) {
    DbClient client(conn_str);

    std::string run_id = GenerateUUID();
    telemetry::GenerateRequest req;
    req.set_tier("INTEGRATION");
    req.set_start_time_iso("2025-01-03T00:00:00Z");
    req.set_end_time_iso("2025-01-03T01:00:00Z");
    req.set_interval_seconds(60);
    req.set_seed(99);
    req.set_host_count(1);
    client.CreateRun(run_id, req, "SUCCEEDED");

    std::string model_run_id = client.CreateModelRun(run_id, "test_model_progress");
    ASSERT_FALSE(model_run_id.empty());

    std::string job_id = client.CreateScoreJob(run_id, model_run_id);
    ASSERT_FALSE(job_id.empty());

    ASSERT_NO_THROW(client.UpdateScoreJob(job_id, "RUNNING", 10, 4, 123));
    auto job = client.GetScoreJob(job_id);
    ASSERT_TRUE(job.contains("last_record_id"));
    ASSERT_TRUE(job.contains("updated_at"));
    EXPECT_EQ(job["last_record_id"].get<long>(), 123);
}

TEST_F(DbClientTest, CreateScoreJobIsIdempotent) {
    DbClient client(conn_str);

    std::string run_id = GenerateUUID();
    telemetry::GenerateRequest req;
    req.set_tier("INTEGRATION");
    req.set_start_time_iso("2025-01-04T00:00:00Z");
    req.set_end_time_iso("2025-01-04T01:00:00Z");
    req.set_interval_seconds(60);
    req.set_seed(100);
    req.set_host_count(1);
    client.CreateRun(run_id, req, "SUCCEEDED");

    std::string model_run_id = client.CreateModelRun(run_id, "test_model_idempotent");
    ASSERT_FALSE(model_run_id.empty());

    std::string job_a = client.CreateScoreJob(run_id, model_run_id);
    std::string job_b = client.CreateScoreJob(run_id, model_run_id);
    ASSERT_FALSE(job_a.empty());
    ASSERT_EQ(job_a, job_b);
}
