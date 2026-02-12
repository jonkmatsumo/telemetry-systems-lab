#include <gtest/gtest.h>
#include "db_client.h"
#include <vector>
#include <string>
#include <chrono>
#include <exception>
#include <thread>
#include <future>
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
auto GenerateUUID() -> std::string {
    uuid_t binuuid;
    uuid_generate_random(binuuid);
    std::string out(36, '\0');
    uuid_unparse_lower(binuuid, out.data());
    return {out};
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
    EXPECT_ANY_THROW(client.CreateRun("id", req, "PENDING"));
    EXPECT_ANY_THROW(client.UpdateRunStatus("id", "RUNNING", 1));
    EXPECT_ANY_THROW(client.Heartbeat(IDbClient::JobType::Generation, "id"));
    EXPECT_ANY_THROW(client.UpdateModelRunStatus("id", "FAILED", "", "write failed"));
    EXPECT_ANY_THROW(client.UpdateInferenceRunStatus("id", "FAILED", 0, nlohmann::json::array(), 0.0));
    EXPECT_ANY_THROW(client.UpdateScoreJob("id", "FAILED", 0, 0, 0, "write failed"));
    EXPECT_ANY_THROW(client.ReconcileStaleJobs());
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
    ASSERT_NO_THROW(client.CreateRun(run_id, req, "PENDING"));
    ASSERT_NO_THROW(client.UpdateRunStatus(run_id, "PENDING", 1));

    std::string model_run_id = client.CreateModelRun(run_id, "test_model", {{"n_components", 3}});
    ASSERT_FALSE(model_run_id.empty());
    ASSERT_NO_THROW(client.UpdateModelRunStatus(model_run_id, "COMPLETED", "/tmp/test_artifact.json"));

    std::string inference_id = client.CreateInferenceRun(model_run_id);
    ASSERT_FALSE(inference_id.empty());
    ASSERT_NO_THROW(client.UpdateInferenceRunStatus(inference_id, "COMPLETED", 0, nlohmann::json::array(), 1.0));

    std::string job_id = client.CreateScoreJob(run_id, model_run_id);
    ASSERT_FALSE(job_id.empty());
    ASSERT_NO_THROW(client.UpdateScoreJob(job_id, "COMPLETED", 10, 10, 0));

    auto runs = client.ListGenerationRuns(50, 0, "PENDING");
    ASSERT_FALSE(runs.empty());

    auto models = client.ListModelRuns(50, 0, "COMPLETED", run_id);
    ASSERT_FALSE(models.empty());

    auto inference_runs = client.ListInferenceRuns(run_id, model_run_id, 50, 0, "COMPLETED");
    ASSERT_FALSE(inference_runs.empty());

    auto jobs = client.ListScoreJobs(50, 0, "COMPLETED", run_id, model_run_id);
    ASSERT_FALSE(jobs.empty());

    auto future_filtered = client.ListGenerationRuns(50, 0, "PENDING", "2999-01-01T00:00:00Z");
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

    std::string model_run_id = client.CreateModelRun(run_id, "test_model_progress", {{"n_components", 3}});
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

    std::string model_run_id = client.CreateModelRun(run_id, "test_model_idempotent", {{"n_components", 3}});
    ASSERT_FALSE(model_run_id.empty());

    std::string job_a = client.CreateScoreJob(run_id, model_run_id);
    std::string job_b = client.CreateScoreJob(run_id, model_run_id);
    ASSERT_FALSE(job_a.empty());
    ASSERT_EQ(job_a, job_b);
}

TEST_F(DbClientTest, CreateScoreJobConcurrentCallsShareSingleActiveJob) {
    DbClient setup_client(conn_str);

    std::string run_id = GenerateUUID();
    telemetry::GenerateRequest req;
    req.set_tier("INTEGRATION");
    req.set_start_time_iso("2025-01-04T00:00:00Z");
    req.set_end_time_iso("2025-01-04T01:00:00Z");
    req.set_interval_seconds(60);
    req.set_seed(101);
    req.set_host_count(1);
    setup_client.CreateRun(run_id, req, "SUCCEEDED");

    std::string model_run_id = setup_client.CreateModelRun(run_id, "test_model_concurrent_idempotent", {{"n_components", 3}});
    ASSERT_FALSE(model_run_id.empty());

    std::promise<void> start_promise;
    std::shared_future<void> start_signal = start_promise.get_future().share();
    
    const int num_threads = 4;
    std::vector<std::string> results(num_threads);
    std::vector<std::exception_ptr> errors(num_threads);
    std::vector<std::thread> threads;

    auto create_job_worker = [&](int idx) {
        try {
            DbClient worker(conn_str);
            start_signal.wait();
            results[idx] = worker.CreateScoreJob(run_id, model_run_id);
        } catch (...) {
            errors[idx] = std::current_exception();
        }
    };

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(create_job_worker, i);
    }
    
    start_promise.set_value();
    for (auto& t : threads) {
        t.join();
    }

    for (int i = 0; i < num_threads; ++i) {
        if (errors[i]) {
            std::rethrow_exception(errors[i]);
        }
        ASSERT_FALSE(results[i].empty());
        if (i > 0) {
            EXPECT_EQ(results[0], results[i]);
        }
    }

    auto jobs = setup_client.ListScoreJobs(50, 0, "", run_id, model_run_id);
    long active_jobs = 0;
    for (const auto& job : jobs) {
        const std::string status = job.value("status", "");
        if (status == "PENDING" || status == "RUNNING") {
            active_jobs++;
        }
    }
    EXPECT_EQ(active_jobs, 1);
}

TEST_F(DbClientTest, InvalidLifecycleStatusWritesFail) {
    DbClient client(conn_str);

    std::string run_id = GenerateUUID();
    telemetry::GenerateRequest req;
    req.set_tier("INTEGRATION");
    req.set_start_time_iso("2025-01-04T00:00:00Z");
    req.set_end_time_iso("2025-01-04T01:00:00Z");
    req.set_interval_seconds(60);
    req.set_seed(102);
    req.set_host_count(1);
    client.CreateRun(run_id, req, "SUCCEEDED");

    std::string model_run_id = client.CreateModelRun(run_id, "test_invalid_status", {{"n_components", 3}});
    ASSERT_FALSE(model_run_id.empty());
    std::string job_id = client.CreateScoreJob(run_id, model_run_id);
    ASSERT_FALSE(job_id.empty());

    EXPECT_ANY_THROW(client.UpdateRunStatus(run_id, "NOT_A_STATUS", 0));
    EXPECT_ANY_THROW(client.UpdateModelRunStatus(model_run_id, "NOT_A_STATUS"));
    EXPECT_ANY_THROW(client.UpdateScoreJob(job_id, "NOT_A_STATUS", 10, 1, 1));
}

TEST_F(DbClientTest, ReconcileStaleJobs) {
    DbClient client(conn_str);
    std::string run_id = GenerateUUID();
    
    // Create a job that is RUNNING
    telemetry::GenerateRequest req;
    req.set_tier("INTEGRATION");
    req.set_start_time_iso("2025-01-05T00:00:00Z");
    req.set_end_time_iso("2025-01-05T01:00:00Z");
    req.set_interval_seconds(60);
    req.set_seed(101);
    req.set_host_count(1);
    client.CreateRun(run_id, req, "SUCCEEDED");

    std::string model_run_id = client.CreateModelRun(run_id, "test_model_reconcile", {{"n_components", 3}});
    std::string job_id = client.CreateScoreJob(run_id, model_run_id);
    client.UpdateScoreJob(job_id, "RUNNING", 100, 50, 50);

    // Call Reconcile
    client.ReconcileStaleJobs();

    // Verify it is FAILED
    auto job = client.GetScoreJob(job_id);
    EXPECT_EQ(job["status"], "FAILED");
    EXPECT_EQ(job["error"], "System restart/recovery");
}

TEST_F(DbClientTest, ReconcileStaleJobsDoesNotFailCompletedGenerationRun) {
    DbClient client(conn_str);
    std::string run_id = GenerateUUID();

    telemetry::GenerateRequest req;
    req.set_tier("INTEGRATION");
    req.set_start_time_iso("2025-01-06T00:00:00Z");
    req.set_end_time_iso("2025-01-06T01:00:00Z");
    req.set_interval_seconds(60);
    req.set_seed(202);
    req.set_host_count(1);
    client.CreateRun(run_id, req, "RUNNING");

    {
        auto scope = client.BeginTransaction("TestSetStaleGenerationHeartbeat");
        scope->txn().exec_params(
            "UPDATE generation_runs SET heartbeat_at = NOW() - INTERVAL '5 minutes' WHERE run_id = $1",
            run_id);
        scope->commit();
    }

    std::exception_ptr completion_error;
    std::exception_ptr reconciliation_error;

    std::thread completion_thread([&]() {
        try {
            client.UpdateRunStatus(run_id, "SUCCEEDED", 42);
        } catch (...) {
            completion_error = std::current_exception();
        }
    });

    std::thread reconciliation_thread([&]() {
        try {
            // Start slightly later to emulate overlap while completion is in-flight.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            client.ReconcileStaleJobs(std::chrono::seconds(1));
        } catch (...) {
            reconciliation_error = std::current_exception();
        }
    });

    completion_thread.join();
    reconciliation_thread.join();

    if (completion_error) {
        std::rethrow_exception(completion_error);
    }
    if (reconciliation_error) {
        std::rethrow_exception(reconciliation_error);
    }

    auto detail = client.GetDatasetDetail(run_id);
    EXPECT_EQ(detail["status"], "SUCCEEDED");
}

TEST_F(DbClientTest, PersistsRequestId) {
    DbClient client(conn_str);
    std::string run_id = GenerateUUID();
    std::string req_id = "test-request-123";
    
    telemetry::GenerateRequest req;
    req.set_tier("PERSISTENCE");
    req.set_start_time_iso("2025-01-01T00:00:00Z");
    req.set_end_time_iso("2025-01-01T01:00:00Z");
    req.set_interval_seconds(60);
    req.set_host_count(1);
    
    // 1. Generation Run
    client.CreateRun(run_id, req, "PENDING", req_id);
    auto detail = client.GetDatasetDetail(run_id);
    EXPECT_EQ(detail["request_id"], req_id);

    // 2. Model Run
    std::string model_run_id = client.CreateModelRun(run_id, "test_persistence", {{"n_components", 3}}, req_id);
    auto model = client.GetModelRun(model_run_id);
    EXPECT_EQ(model["request_id"], req_id);

    // 3. Score Job
    std::string job_id = client.CreateScoreJob(run_id, model_run_id, req_id);
    auto job = client.GetScoreJob(job_id);
    EXPECT_EQ(job["request_id"], req_id);
}

TEST_F(DbClientTest, DeleteDatasetWithScores) {
    DbClient client(conn_str);
    std::string run_id = GenerateUUID();
    
    telemetry::GenerateRequest req;
    req.set_tier("DELETE_TEST");
    req.set_start_time_iso("2025-01-01T00:00:00Z");
    req.set_end_time_iso("2025-01-01T01:00:00Z");
    req.set_interval_seconds(60);
    req.set_host_count(1);
    
    // 1. Create Data
    client.CreateRun(run_id, req, "SUCCEEDED");
    
    TelemetryRecord rec;
    rec.run_id = run_id;
    rec.metric_timestamp = std::chrono::system_clock::now();
    rec.ingestion_time = rec.metric_timestamp;
    rec.host_id = "host-1";
    rec.project_id = "proj-1";
    rec.region = "us-test";
    rec.labels_json = "{}";
    rec.cpu_usage = 10.0;
    rec.memory_usage = 20.0;
    rec.disk_utilization = 30.0;
    rec.network_rx_rate = 40.0;
    rec.network_tx_rate = 50.0;
    rec.is_anomaly = false;
    client.BatchInsertTelemetry({rec});
    
    // Need a record_id to insert scores. Fetch it back.
    auto rows = client.FetchScoringRowsAfterRecord(run_id, 0, 10);
    ASSERT_FALSE(rows.empty());
    long record_id = rows[0].record_id;
    
    std::string model_run_id = client.CreateModelRun(run_id, "test_delete", {{"n_components", 3}});
    std::string job_id = client.CreateScoreJob(run_id, model_run_id);
    
    // Insert a score
    client.InsertDatasetScores(run_id, model_run_id, {{record_id, {0.5, false}}});
    
    // 2. Verify existence
    auto detail = client.GetDatasetDetail(run_id);
    EXPECT_EQ(detail["run_id"], run_id);
    
    auto scores = client.GetScores(run_id, model_run_id, 10, 0, false, 0, 0);
    EXPECT_EQ(scores["total"], 1);
    EXPECT_FALSE(scores["has_more"].get<bool>()); // 1 < 10
    
    // 3. Delete
    ASSERT_NO_THROW(client.DeleteDatasetWithScores(run_id));
    
    // 4. Verify deletion
    auto detail_after = client.GetDatasetDetail(run_id);
    EXPECT_TRUE(detail_after.empty() || detail_after.is_null());
    
    // Check scores too
    auto scores_after = client.GetScores(run_id, model_run_id, 10, 0, false, 0, 0);
    EXPECT_EQ(scores_after["total"], 0);
}

TEST_F(DbClientTest, GetTopKTruncation) {
    DbClient client(conn_str);
    std::string run_id = GenerateUUID();
    
    telemetry::GenerateRequest req;
    req.set_tier("TOPK_TEST");
    req.set_start_time_iso("2026-02-01T00:00:00Z");
    req.set_end_time_iso("2026-02-01T01:00:00Z");
    req.set_interval_seconds(60);
    req.set_host_count(1);
    client.CreateRun(run_id, req, "SUCCEEDED");
    
    // Insert 3 different regions
    TelemetryRecord rec;
    rec.run_id = run_id;
    rec.metric_timestamp = std::chrono::system_clock::now();
    rec.ingestion_time = rec.metric_timestamp;
    rec.host_id = "host-1";
    rec.project_id = "proj-1";
    rec.labels_json = "{}";
    rec.cpu_usage = 10.0;
    rec.memory_usage = 20.0;
    rec.disk_utilization = 30.0;
    rec.network_rx_rate = 40.0;
    rec.network_tx_rate = 50.0;
    rec.is_anomaly = false;
    
    rec.region = "region-1";
    client.BatchInsertTelemetry({rec});
    rec.region = "region-2";
    client.BatchInsertTelemetry({rec});
    rec.region = "region-3";
    client.BatchInsertTelemetry({rec});
    
    // Request Top 2
    auto top2 = client.GetTopK(run_id, "region", 2, "", "", "", "", "", false);
    EXPECT_EQ(top2["items"].size(), 2);
    EXPECT_TRUE(top2["truncated"].get<bool>()); // 3 > 2
    
    // Request Top 5
    auto top5 = client.GetTopK(run_id, "region", 5, "", "", "", "", "", false);
    EXPECT_EQ(top5["items"].size(), 3);
    EXPECT_FALSE(top5["truncated"].get<bool>()); // 3 < 5
}
