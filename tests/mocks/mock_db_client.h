#pragma once
#include "idb_client.h"
#include <gmock/gmock.h>
#include <vector>
#include <string>
#include <mutex>
#include <map>

class MockDbClient : public IDbClient {
public:
    std::shared_ptr<DbConnectionManager> GetConnectionManager() override {
        return std::make_shared<SimpleDbConnectionManager>("dummy");
    }

    MOCK_METHOD(void, ReconcileStaleJobs, (std::optional<std::chrono::seconds> stale_ttl), (override));
    MOCK_METHOD(void, EnsurePartition, (std::chrono::system_clock::time_point tp), (override));
    MOCK_METHOD(void, CreateRun, (const std::string& run_id, const telemetry::GenerateRequest& config, const std::string& status, const std::string& request_id), (override));
    MOCK_METHOD(void, UpdateRunStatus, (const std::string& run_id, const std::string& status, long inserted_rows, const std::string& error), (override));
    MOCK_METHOD(void, BatchInsertTelemetry, (const std::vector<TelemetryRecord>& records), (override));
    MOCK_METHOD(void, Heartbeat, (JobType type, const std::string& job_id), (override));
    MOCK_METHOD(telemetry::RunStatus, GetRunStatus, (const std::string& run_id), (override));

    // Keep some manual implementations if needed for other tests, or convert all.
    
    std::string CreateModelRun(const std::string& /*dataset_id*/, 
                               const std::string& /*name*/,
                               const nlohmann::json& /*training_config*/ = {},
                               const std::string& /*request_id*/ = "",
                               const nlohmann::json& /*hpo_config*/ = nlohmann::json::object(),
                               const std::string& /*candidate_fingerprint*/ = "",
                               const std::string& /*generator_version*/ = "",
                               std::optional<long long> /*seed_used*/ = std::nullopt) override {
        return "mock-model-run-id";
    }

    std::string CreateHpoTrialRun(const std::string& /*dataset_id*/,
                                  const std::string& /*name*/,
                                  const nlohmann::json& /*training_config*/,
                                  const std::string& /*request_id*/,
                                  const std::string& /*parent_run_id*/,
                                  int /*trial_index*/,
                                  const nlohmann::json& /*trial_params*/) override {
        return "mock-trial-id";
    }

    void UpdateModelRunStatus(const std::string& model_run_id, 
                              const std::string& status, 
                              const std::string& /*artifact_path*/ = "", 
                              const std::string& /*error*/ = "",
                              const nlohmann::json& /*error_summary*/ = nlohmann::json()) override {
        std::lock_guard<std::mutex> lock(mutex_);
        last_model_run_id = model_run_id;
        last_model_run_status = status;
        model_run_statuses[model_run_id] = status;
    }

    bool TryTransitionModelRunStatus(const std::string& model_run_id,
                                     const std::string& expected_current,
                                     const std::string& next_status) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (model_run_statuses[model_run_id] == expected_current || (model_run_statuses[model_run_id].empty() && expected_current == "PENDING")) {
            model_run_statuses[model_run_id] = next_status;
            return true;
        }
        return false;
    }

    nlohmann::json GetModelRun(const std::string& model_run_id) override {
        nlohmann::json j;
        j["model_run_id"] = model_run_id;
        j["status"] = "COMPLETED";
        j["artifact_path"] = mock_artifact_path;
        return j;
    }

    int get_hpo_trials_count = 0;
    nlohmann::json GetHpoTrials(const std::string& /*parent_run_id*/) override {
        get_hpo_trials_count++;
        return nlohmann::json::array();
    }

    nlohmann::json GetHpoTrialsPaginated(const std::string& /*parent_run_id*/, int /*limit*/, int /*offset*/) override {
        return nlohmann::json::array();
    }

    int get_bulk_hpo_count = 0;
    std::map<std::string, nlohmann::json> GetBulkHpoTrialSummaries(const std::vector<std::string>& parent_run_ids) override {
        get_bulk_hpo_count++;
        std::map<std::string, nlohmann::json> ret;
        for (const auto& id : parent_run_ids) {
            // Return dummy summary for performance test
             ret[id] = {
                 {"trial_count", 10},
                 {"completed_count", 10},
                 {"status_counts", {{"COMPLETED", 10}}}
             };
        }
        return ret;
    }

    void UpdateBestTrial(const std::string& /*parent_run_id*/,
                         const std::string& /*best_trial_run_id*/,
                         double /*best_metric_value*/,
                         const std::string& /*best_metric_name*/,
                         const std::string& /*best_metric_direction*/,
                         const std::string& /*tie_break_basis*/) override {
        // No-op
    }

    std::string CreateInferenceRun(const std::string& /*model_run_id*/) override {
        return "mock-inference-id";
    }

    void UpdateInferenceRunStatus(const std::string& /*inference_id*/, 
                                  const std::string& /*status*/, 
                                  int /*anomaly_count*/, 
                                  const nlohmann::json& /*details*/ = {},
                                  double /*latency_ms*/ = 0.0) override {
        // No-op
    }

    void UpdateTrialEligibility(const std::string& /*model_run_id*/,
                                bool /*is_eligible*/,
                                const std::string& /*reason*/,
                                double /*metric_value*/,
                                const std::string& /*source*/ = "") override {
        // No-op
    }

    void UpdateParentErrorAggregates(const std::string& /*parent_run_id*/,
                                     const nlohmann::json& /*error_aggregates*/) override {
        // No-op
    }

    void InsertDatasetScores(const std::string& /*dataset_id*/,
                             const std::string& /*model_run_id*/,
                             const std::vector<std::pair<long, std::pair<double, bool>>>& /*scores*/) override {
        if (should_fail_insert) {
            throw std::runtime_error("Simulated insert failure");
        }
    }

    long GetDatasetRecordCount(const std::string& /*dataset_id*/) override {
        return 100;
    }

    nlohmann::json ListGenerationRuns(int /*limit*/,
                                      int /*offset*/,
                                      const std::string& /*status*/ = "",
                                      const std::string& /*created_from*/ = "",
                                      const std::string& /*created_to*/ = "") override {
        return nlohmann::json::array();
    }
    nlohmann::json GetDatasetDetail(const std::string& /*run_id*/) override { return {}; }
    nlohmann::json GetDatasetSamples(const std::string& /*run_id*/, int /*limit*/) override { return nlohmann::json::array(); }
    nlohmann::json SearchDatasetRecords(const std::string& /*run_id*/,
                                        int /*limit*/,
                                        int /*offset*/,
                                        const std::string& /*start_time*/,
                                        const std::string& /*end_time*/,
                                        const std::string& /*is_anomaly*/,
                                        const std::string& /*anomaly_type*/,
                                        const std::string& /*host_id*/,
                                        const std::string& /*region*/,
                                        const std::string& /*sort_by*/,
                                        const std::string& /*sort_order*/,
                                        const std::string& /*anchor_time*/) override {
        return nlohmann::json::array();
    }
    nlohmann::json GetDatasetRecord(const std::string& /*run_id*/, long /*record_id*/) override { return {}; }
    nlohmann::json GetMetricStats(const std::string& /*run_id*/, const std::string& /*metric*/) override { return {}; }
    nlohmann::json GetDatasetMetricsSummary(const std::string& /*run_id*/) override { return {}; }
    nlohmann::json GetModelsForDataset(const std::string& /*dataset_id*/) override { return nlohmann::json::array(); }
    nlohmann::json ListModelRuns(int /*limit*/,
                                 int /*offset*/,
                                 const std::string& /*status*/ = "",
                                 const std::string& /*dataset_id*/ = "",
                                 const std::string& /*created_from*/ = "",
                                 const std::string& /*created_to*/ = "") override {
        return nlohmann::json::array();
    }
    nlohmann::json GetScoredDatasetsForModel(const std::string& /*model_run_id*/) override { return nlohmann::json::array(); }
    nlohmann::json GetScores(const std::string& /*dataset_id*/,
                             const std::string& /*model_run_id*/,
                             int /*limit*/,
                             int /*offset*/,
                             bool /*only_anomalies*/,
                             double /*min_score*/,
                             double /*max_score*/) override {
        return nlohmann::json::array();
    }
    nlohmann::json ListInferenceRuns(const std::string& /*dataset_id*/,
                                     const std::string& /*model_run_id*/,
                                     int /*limit*/,
                                     int /*offset*/,
                                     const std::string& /*status*/ = "",
                                     const std::string& /*created_from*/ = "",
                                     const std::string& /*created_to*/ = "") override {
        return nlohmann::json::array();
    }
    nlohmann::json GetInferenceRun(const std::string& /*inference_id*/) override { return {}; }
    nlohmann::json GetEvalMetrics(const std::string& /*dataset_id*/,
                                  const std::string& /*model_run_id*/,
                                  int /*points*/,
                                  int /*max_samples*/) override {
        return {};
    }
    nlohmann::json GetErrorDistribution(const std::string& /*dataset_id*/,
                                        const std::string& /*model_run_id*/,
                                        const std::string& /*group_by*/) override {
        return {};
    }

    nlohmann::json GetDatasetSummary(const std::string& /*run_id*/, int /*topk*/) override { return {}; }
    nlohmann::json GetTopK(const std::string& /*run_id*/,
                           const std::string& /*column*/,
                           int /*k*/,
                           const std::string& /*region*/,
                           const std::string& /*is_anomaly*/,
                           const std::string& /*anomaly_type*/,
                           const std::string& /*start_time*/,
                           const std::string& /*end_time*/,
                           bool /*include_total_distinct*/ = false) override {
        return {};
    }
    nlohmann::json GetTimeSeries(const std::string& /*run_id*/,
                                 const std::vector<std::string>& /*metrics*/,
                                 const std::vector<std::string>& /*aggs*/,
                                 int /*bucket_seconds*/,
                                 const std::string& /*region*/,
                                 const std::string& /*is_anomaly*/,
                                 const std::string& /*anomaly_type*/,
                                 const std::string& /*start_time*/,
                                 const std::string& /*end_time*/) override {
        return {};
    }
    nlohmann::json GetHistogram(const std::string& /*run_id*/,
                                const std::string& /*metric*/,
                                int /*bins*/,
                                double /*min_val*/,
                                double /*max_val*/,
                                const std::string& /*region*/,
                                const std::string& /*is_anomaly*/,
                                const std::string& /*anomaly_type*/,
                                const std::string& /*start_time*/,
                                const std::string& /*end_time*/) override {
        return {};
    }

    void UpdateScoreJob(const std::string& job_id,
                        const std::string& status,
                        long /*total_rows*/,
                        long /*processed_rows*/,
                        long /*last_record_id*/ = 0,
                        const std::string& error = "") override {
        last_job_id = job_id;
        last_job_status = status;
        last_job_error = error;
        job_statuses[job_id] = status;
    }

    bool TryTransitionScoreJobStatus(const std::string& job_id,
                                     const std::string& expected_current,
                                     const std::string& next_status) override {
        if (job_statuses[job_id] == expected_current || (job_statuses[job_id].empty() && expected_current == "PENDING")) {
            job_statuses[job_id] = next_status;
            last_job_status = next_status;
            return true;
        }
        return false;
    }

    nlohmann::json GetScoreJob(const std::string& job_id) override {
        nlohmann::json j;
        j["job_id"] = job_id;
        j["status"] = last_job_status.empty() ? "PENDING" : last_job_status;
        j["total_rows"] = 100;
        j["processed_rows"] = 0;
        j["last_record_id"] = 0;
        return j;
    }

    std::vector<ScoringRow> FetchScoringRowsAfterRecord(const std::string& /*dataset_id*/,
                                                        long last_record_id,
                                                        int limit) override {
        if (should_fail_fetch) {
            throw std::runtime_error("Simulated fetch failure");
        }
        if (last_record_id >= 100) return {};
        std::vector<ScoringRow> rows;
        for (int i = 0; i < std::min(limit, 10); ++i) {
            ScoringRow r;
            r.record_id = last_record_id + i + 1;
            rows.push_back(r);
        }
        return rows;
    }

    std::string CreateScoreJob(const std::string& /*dataset_id*/, 
                               const std::string& /*model_run_id*/,
                               const std::string& /*request_id*/ = "") override {
        return "mock-score-job-id";
    }

    nlohmann::json ListScoreJobs(int /*limit*/,
                                 int /*offset*/,
                                 const std::string& /*status*/ = "",
                                 const std::string& /*dataset_id*/ = "",
                                 const std::string& /*model_run_id*/ = "",
                                 const std::string& /*created_from*/ = "",
                                 const std::string& /*created_to*/ = "") override {
        return nlohmann::json::array();
    }

    // Inspection helpers
    bool should_fail_insert = false;
    bool should_fail_fetch = false;
    std::string mock_artifact_path = "artifacts/pca/default/model.json";
    std::string last_job_id;
    std::string last_job_status;
    std::string last_job_error;
    std::string last_model_run_id;
    std::string last_model_run_status;
    std::map<std::string, std::string> model_run_statuses; // Store status per ID
    std::map<std::string, std::string> job_statuses;
    std::mutex mutex_;
    size_t last_batch_size = 0;
    TelemetryRecord last_record;
};
