#pragma once
#include "idb_client.h"
#include "db_connection_manager.h"
#include <pqxx/pqxx>
#include <optional>
#include <string>
#include <vector>

class DbClient : public IDbClient {
public:
    DbClient(const std::string& connection_string);
    DbClient(std::shared_ptr<DbConnectionManager> manager);
    auto operator=(const DbClient&) -> DbClient& = delete;
    DbClient(const DbClient&) = delete;
    ~DbClient() override = default;

    // Validates that a metric name is a known telemetry column.
    // Returns true only for columns that exist in host_telemetry_archival schema.
    static auto IsValidMetric(const std::string& metric) -> bool;

    // Validates that a dimension name is an allowed grouping/filtering column.
    static auto IsValidDimension(const std::string& dim) -> bool;

    // Validates that an aggregation function is allowed.
    static auto IsValidAggregation(const std::string& agg) -> bool;

    // Prepares named statements for a connection.
    static auto PrepareStatements(pqxx::connection& C) -> void;

    // Marks any 'RUNNING' or 'PENDING' jobs as 'FAILED' if they are stale.
    auto ReconcileStaleJobs(std::optional<std::chrono::seconds> stale_ttl = std::nullopt) -> void override;

    auto GetConnectionManager() -> std::shared_ptr<DbConnectionManager> override {
        return manager_;
    }

    // Runs the retention cleanup procedure.
    auto RunRetentionCleanup(int retention_days) -> void;

    // Ensures a partition exists for the given timestamp.
    auto EnsurePartition(std::chrono::system_clock::time_point tp) -> void override;

    auto DeleteDatasetWithScores(const std::string& dataset_id) -> void;

    auto CreateRun(const std::string& run_id, 
                   const telemetry::GenerateRequest& config, 
                   const std::string& status,
                   const std::string& request_id = "") -> void override;
                   
    auto UpdateRunStatus(const std::string& run_id, 
                         const std::string& status, 
                         long inserted_rows,
                         const std::string& error = "") -> void override;

    auto BatchInsertTelemetry(const std::vector<TelemetryRecord>& records) -> void override;
    
    auto Heartbeat(JobType type, const std::string& job_id) -> void override;
    
    auto GetRunStatus(const std::string& run_id) -> telemetry::RunStatus override;

    auto CreateModelRun(const std::string& dataset_id, 
                               const std::string& name,
                               const nlohmann::json& training_config = {},
                               const std::string& request_id = "",
                               const nlohmann::json& hpo_config = nlohmann::json::object(),
                               const std::string& candidate_fingerprint = "",
                               const std::string& generator_version = "",
                               std::optional<long long> seed_used = std::nullopt) -> std::string override;
                               
    auto CreateHpoTrialRun(const std::string& dataset_id,
                                  const std::string& name,
                                  const nlohmann::json& training_config,
                                  const std::string& request_id,
                                  const std::string& parent_run_id,
                                  int trial_index,
                                  const nlohmann::json& trial_params) -> std::string override;

    auto UpdateModelRunStatus(const std::string& model_run_id, 
                                      const std::string& status, 
                                      const std::string& artifact_path = "", 
                                      const std::string& error = "",
                                      const nlohmann::json& error_summary = nlohmann::json()) -> void override;
    
    auto TryTransitionModelRunStatus(const std::string& model_run_id,
                                     const std::string& expected_current,
                                     const std::string& next_status) -> bool override;

    auto GetModelRun(const std::string& model_run_id) -> nlohmann::json override;
    auto GetHpoTrials(const std::string& parent_run_id) -> nlohmann::json override;
    auto GetHpoTrialsPaginated(const std::string& parent_run_id, int limit, int offset) -> nlohmann::json override;
    auto GetBulkHpoTrialSummaries(const std::vector<std::string>& parent_run_ids) -> std::map<std::string, nlohmann::json> override;
    auto UpdateBestTrial(const std::string& parent_run_id,
                         const std::string& best_trial_run_id,
                         double best_metric_value,
                         const std::string& best_metric_name,
                         const std::string& best_metric_direction,
                         const std::string& tie_break_basis) -> void override;

    auto CreateInferenceRun(const std::string& model_run_id) -> std::string override;
    auto UpdateInferenceRunStatus(const std::string& inference_id, 
                                          const std::string& status, 
                                          int anomaly_count, 
                                          const nlohmann::json& details = {},
                                          double latency_ms = 0.0) -> void override;

    auto UpdateTrialEligibility(const std::string& model_run_id,
                                bool is_eligible,
                                const std::string& reason,
                                double metric_value,
                                const std::string& source = "") -> void override;

    auto UpdateParentErrorAggregates(const std::string& parent_run_id,
                                     const nlohmann::json& error_aggregates) -> void override;

    auto InsertAlert(const Alert& alert) -> void;

    auto ListGenerationRuns(int limit,
                                      int offset,
                                      const std::string& status = "",
                                      const std::string& created_from = "",
                                      const std::string& created_to = "") -> nlohmann::json override;
    auto GetDatasetDetail(const std::string& run_id) -> nlohmann::json override;
    auto GetDatasetSamples(const std::string& run_id, int limit) -> nlohmann::json override;
    auto SearchDatasetRecords(const std::string& run_id,
                                        int limit,
                                        int offset,
                                        const std::string& start_time,
                                        const std::string& end_time,
                                        const std::string& is_anomaly,
                                        const std::string& anomaly_type,
                                        const std::string& host_id,
                                        const std::string& region,
                                        const std::string& sort_by,
                                        const std::string& sort_order,
                                        const std::string& anchor_time) -> nlohmann::json override;
    auto GetDatasetRecord(const std::string& run_id, long record_id) -> nlohmann::json override;
    auto ListModelRuns(int limit,
                                 int offset,
                                 const std::string& status = "",
                                 const std::string& dataset_id = "",
                                 const std::string& created_from = "",
                                 const std::string& created_to = "") -> nlohmann::json override;
    auto ListInferenceRuns(const std::string& dataset_id,
                                     const std::string& model_run_id,
                                     int limit,
                                     int offset,
                                     const std::string& status = "",
                                     const std::string& created_from = "",
                                     const std::string& created_to = "") -> nlohmann::json override;
    auto GetInferenceRun(const std::string& inference_id) -> nlohmann::json override;

    auto GetModelsForDataset(const std::string& dataset_id) -> nlohmann::json override;
    auto GetScoredDatasetsForModel(const std::string& model_run_id) -> nlohmann::json override;

    auto GetDatasetSummary(const std::string& run_id, int topk) -> nlohmann::json override;
    auto GetTopK(const std::string& run_id,
                           const std::string& column,
                           int k,
                           const std::string& region,
                           const std::string& is_anomaly,
                           const std::string& anomaly_type,
                           const std::string& start_time,
                           const std::string& end_time,
                           bool include_total_distinct = false) -> nlohmann::json override;
    auto GetTimeSeries(const std::string& run_id,
                                 const std::vector<std::string>& metrics,
                                 const std::vector<std::string>& aggs,
                                 int bucket_seconds,
                                 const std::string& region,
                                 const std::string& is_anomaly,
                                 const std::string& anomaly_type,
                                 const std::string& start_time,
                                 const std::string& end_time) -> nlohmann::json override;
    auto GetHistogram(const std::string& run_id,
                                const std::string& metric,
                                int bins,
                                double min_val,
                                double max_val,
                                const std::string& region,
                                const std::string& is_anomaly,
                                const std::string& anomaly_type,
                                const std::string& start_time,
                                const std::string& end_time) -> nlohmann::json override;
    auto GetMetricStats(const std::string& run_id, const std::string& metric) -> nlohmann::json override;
    auto GetDatasetMetricsSummary(const std::string& run_id) -> nlohmann::json override;

    auto CreateScoreJob(const std::string& dataset_id, 
                               const std::string& model_run_id,
                               const std::string& request_id = "") -> std::string override;
    auto UpdateScoreJob(const std::string& job_id,
                        const std::string& status,
                        long total_rows,
                        long processed_rows,
                        long last_record_id = 0,
                        const std::string& error = "") -> void override;

    auto TryTransitionScoreJobStatus(const std::string& job_id,
                                     const std::string& expected_current,
                                     const std::string& next_status) -> bool override;

    auto GetScoreJob(const std::string& job_id) -> nlohmann::json override;
    auto ListScoreJobs(int limit,
                                 int offset,
                                 const std::string& status = "",
                                 const std::string& dataset_id = "",
                                 const std::string& model_run_id = "",
                                 const std::string& created_from = "",
                                 const std::string& created_to = "") -> nlohmann::json override;

    auto FetchScoringRowsAfterRecord(const std::string& dataset_id,
                                                        long last_record_id,
                                                        int limit) -> std::vector<IDbClient::ScoringRow> override;
    auto InsertDatasetScores(const std::string& dataset_id,
                             const std::string& model_run_id,
                             const std::vector<std::pair<long, std::pair<double, bool>>>& scores) -> void override;

    auto GetDatasetRecordCount(const std::string& dataset_id) -> long override;

    auto GetScores(const std::string& dataset_id,
                             const std::string& model_run_id,
                             int limit,
                             int offset,
                             bool only_anomalies,
                             double min_score,
                             double max_score) -> nlohmann::json override;

    auto GetEvalMetrics(const std::string& dataset_id,
                                  const std::string& model_run_id,
                                  int points,
                                  int max_samples) -> nlohmann::json override;
    auto GetErrorDistribution(const std::string& dataset_id,
                                        const std::string& model_run_id,
                                        const std::string& group_by) -> nlohmann::json override;

private:
    std::shared_ptr<DbConnectionManager> manager_;
};