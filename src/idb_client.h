#pragma once
#include "types.h"
#include "db_connection_manager.h"
#include <vector>
#include <string>
#include <optional>
#include <nlohmann/json.hpp>
#include "telemetry.grpc.pb.h"

class IDbClient {
public:
    virtual ~IDbClient() = default;

    virtual auto GetConnectionManager() -> std::shared_ptr<DbConnectionManager> = 0;

    // Marks any 'RUNNING' or 'PENDING' jobs as 'FAILED' if they are stale.
    // If stale_ttl is provided, only jobs not updated within that time are reconciled.
    // If nullopt, all non-terminal jobs are reconciled (useful on startup).
    virtual auto ReconcileStaleJobs(std::optional<std::chrono::seconds> stale_ttl = std::nullopt) -> void = 0;
    
    virtual auto EnsurePartition(std::chrono::system_clock::time_point tp) -> void = 0;

    virtual auto CreateRun(const std::string& run_id, 
                           const telemetry::GenerateRequest& config, 
                           const std::string& status,
                           const std::string& request_id = "") -> void = 0;
                   
    virtual auto UpdateRunStatus(const std::string& run_id, 
                                 const std::string& status, 
                                 long inserted_rows,
                                 const std::string& error = "") -> void = 0;

    virtual auto BatchInsertTelemetry(const std::vector<TelemetryRecord>& records) -> void = 0;

    enum class JobType {
        Generation,
        ModelRun,
        ScoreJob
    };
    virtual auto Heartbeat(JobType type, const std::string& job_id) -> void = 0;

    virtual auto GetRunStatus(const std::string& run_id) -> telemetry::RunStatus = 0;

    virtual auto CreateModelRun(const std::string& dataset_id, 
                                       const std::string& name,
                                       const nlohmann::json& training_config = {},
                                       const std::string& request_id = "",
                                       const nlohmann::json& hpo_config = nlohmann::json::object(),
                                       const std::string& candidate_fingerprint = "",
                                       const std::string& generator_version = "",
                                       std::optional<long long> seed_used = std::nullopt) -> std::string = 0;
    
    virtual auto CreateHpoTrialRun(const std::string& dataset_id,
                                          const std::string& name,
                                          const nlohmann::json& training_config,
                                          const std::string& request_id,
                                          const std::string& parent_run_id,
                                          int trial_index,
                                          const nlohmann::json& trial_params) -> std::string = 0;

    virtual auto UpdateModelRunStatus(const std::string& model_run_id, 
                                      const std::string& status, 
                                      const std::string& artifact_path = "", 
                                      const std::string& error = "",
                                      const nlohmann::json& error_summary = nlohmann::json()) -> void = 0;
    
    virtual auto TryTransitionModelRunStatus(const std::string& model_run_id,
                                             const std::string& expected_current,
                                             const std::string& next_status) -> bool = 0;

    virtual auto GetModelRun(const std::string& model_run_id) -> nlohmann::json = 0;
    virtual auto GetHpoTrials(const std::string& parent_run_id) -> nlohmann::json = 0;
    virtual auto GetHpoTrialsPaginated(const std::string& parent_run_id, int limit, int offset) -> nlohmann::json = 0;
    virtual auto GetBulkHpoTrialSummaries(const std::vector<std::string>& parent_run_ids) -> std::map<std::string, nlohmann::json> = 0;
    virtual auto UpdateBestTrial(const std::string& parent_run_id,
                                 const std::string& best_trial_run_id,
                                 double best_metric_value,
                                 const std::string& best_metric_name,
                                 const std::string& best_metric_direction,
                                 const std::string& tie_break_basis) -> void = 0;

    virtual auto CreateInferenceRun(const std::string& model_run_id) -> std::string = 0;
    virtual auto UpdateInferenceRunStatus(const std::string& inference_id, 
                                          const std::string& status, 
                                          int anomaly_count, 
                                          const nlohmann::json& details = {},
                                          double latency_ms = 0.0) -> void = 0;
    
    virtual auto UpdateTrialEligibility(const std::string& model_run_id,
                                        bool is_eligible,
                                        const std::string& reason,
                                        double metric_value,
                                        const std::string& source = "") -> void = 0;

    virtual auto UpdateParentErrorAggregates(const std::string& parent_run_id,
                                             const nlohmann::json& error_aggregates) -> void = 0;

    virtual auto InsertDatasetScores(const std::string& dataset_id,
                                     const std::string& model_run_id,
                                     const std::vector<std::pair<long, std::pair<double, bool>>>& scores) -> void = 0;

    virtual auto GetDatasetRecordCount(const std::string& dataset_id) -> long = 0;

    virtual auto ListGenerationRuns(int limit,
                                              int offset,
                                              const std::string& status = "",
                                              const std::string& created_from = "",
                                              const std::string& created_to = "") -> nlohmann::json = 0;
    virtual auto GetDatasetDetail(const std::string& run_id) -> nlohmann::json = 0;
    virtual auto GetDatasetSamples(const std::string& run_id, int limit) -> nlohmann::json = 0;
    virtual auto GetDatasetSummary(const std::string& run_id, int topk) -> nlohmann::json = 0;
    virtual auto GetTopK(const std::string& run_id,
                                   const std::string& column,
                                   int k,
                                   const std::string& region,
                                   const std::string& is_anomaly,
                                   const std::string& anomaly_type,
                                   const std::string& start_time,
                                   const std::string& end_time,
                                   bool include_total_distinct = false) -> nlohmann::json = 0;
    virtual auto GetTimeSeries(const std::string& run_id,
                                         const std::vector<std::string>& metrics,
                                         const std::vector<std::string>& aggs,
                                         int bucket_seconds,
                                         const std::string& region,
                                         const std::string& is_anomaly,
                                         const std::string& anomaly_type,
                                         const std::string& start_time,
                                         const std::string& end_time) -> nlohmann::json = 0;
    virtual auto GetHistogram(const std::string& run_id,
                                        const std::string& metric,
                                        int bins,
                                        double min_val,
                                        double max_val,
                                        const std::string& region,
                                        const std::string& is_anomaly,
                                        const std::string& anomaly_type,
                                        const std::string& start_time,
                                        const std::string& end_time) -> nlohmann::json = 0;
    virtual auto SearchDatasetRecords(const std::string& run_id,
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
                                                const std::string& anchor_time) -> nlohmann::json = 0;
    virtual auto GetDatasetRecord(const std::string& run_id, long record_id) -> nlohmann::json = 0;
    virtual auto GetMetricStats(const std::string& run_id, const std::string& metric) -> nlohmann::json = 0;
    virtual auto GetDatasetMetricsSummary(const std::string& run_id) -> nlohmann::json = 0;
    virtual auto GetModelsForDataset(const std::string& dataset_id) -> nlohmann::json = 0;
    virtual auto ListModelRuns(int limit,
                                         int offset,
                                         const std::string& status = "",
                                         const std::string& dataset_id = "",
                                         const std::string& created_from = "",
                                         const std::string& created_to = "") -> nlohmann::json = 0;
    virtual auto GetScoredDatasetsForModel(const std::string& model_run_id) -> nlohmann::json = 0;
    virtual auto GetScores(const std::string& dataset_id,
                                     const std::string& model_run_id,
                                     int limit,
                                     int offset,
                                     bool only_anomalies,
                                     double min_score,
                                     double max_score) -> nlohmann::json = 0;
    virtual auto ListInferenceRuns(const std::string& dataset_id,
                                             const std::string& model_run_id,
                                             int limit,
                                             int offset,
                                             const std::string& status = "",
                                             const std::string& created_from = "",
                                             const std::string& created_to = "") -> nlohmann::json = 0;
    virtual auto GetInferenceRun(const std::string& inference_id) -> nlohmann::json = 0;
    virtual auto GetEvalMetrics(const std::string& dataset_id,
                                          const std::string& model_run_id,
                                          int points,
                                          int max_samples) -> nlohmann::json = 0;
    virtual auto GetErrorDistribution(const std::string& dataset_id,
                                                const std::string& model_run_id,
                                                const std::string& group_by) -> nlohmann::json = 0;

    virtual auto CreateScoreJob(const std::string& dataset_id, 
                                       const std::string& model_run_id,
                                       const std::string& request_id = "") -> std::string = 0;
    virtual auto UpdateScoreJob(const std::string& job_id,
                            const std::string& status,
                            long total_rows,
                            long processed_rows,
                            long last_record_id = 0,
                            const std::string& error = "") -> void = 0;
    
    virtual auto TryTransitionScoreJobStatus(const std::string& job_id,
                                                 const std::string& expected_current,
                                                 const std::string& next_status) -> bool = 0;
    
    virtual auto GetScoreJob(const std::string& job_id) -> nlohmann::json = 0;
    
    virtual auto ListScoreJobs(int limit,
                                         int offset,
                                         const std::string& status = "",
                                         const std::string& dataset_id = "",
                                         const std::string& model_run_id = "",
                                         const std::string& created_from = "",
                                         const std::string& created_to = "") -> nlohmann::json = 0;

    struct ScoringRow {
        long record_id = 0;
        bool is_anomaly = false;
        double cpu = 0.0;
        double mem = 0.0;
        double disk = 0.0;
        double rx = 0.0;
        double tx = 0.0;
    };
    virtual auto FetchScoringRowsAfterRecord(const std::string& dataset_id,
                                                                long last_record_id,
                                                                int limit) -> std::vector<ScoringRow> = 0;
};