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

    virtual std::shared_ptr<DbConnectionManager> GetConnectionManager() = 0;

    virtual void ReconcileStaleJobs() = 0;
    virtual void EnsurePartition(std::chrono::system_clock::time_point tp) = 0;

    virtual void CreateRun(const std::string& run_id, 
                           const telemetry::GenerateRequest& config, 
                           const std::string& status,
                           const std::string& request_id = "") = 0;
                   
    virtual void UpdateRunStatus(const std::string& run_id, 
                                 const std::string& status, 
                                 long inserted_rows,
                                 const std::string& error = "") = 0;

    virtual void BatchInsertTelemetry(const std::vector<TelemetryRecord>& records) = 0;

    virtual telemetry::RunStatus GetRunStatus(const std::string& run_id) = 0;

    virtual std::string CreateModelRun(const std::string& dataset_id, 
                                       const std::string& name,
                                       const nlohmann::json& training_config = {},
                                       const std::string& request_id = "",
                                       const nlohmann::json& hpo_config = nlohmann::json::object(),
                                       const std::string& candidate_fingerprint = "",
                                       const std::string& generator_version = "",
                                       std::optional<long long> seed_used = std::nullopt) = 0;
    
    virtual std::string CreateHpoTrialRun(const std::string& dataset_id,
                                          const std::string& name,
                                          const nlohmann::json& training_config,
                                          const std::string& request_id,
                                          const std::string& parent_run_id,
                                          int trial_index,
                                          const nlohmann::json& trial_params) = 0;

    virtual void UpdateModelRunStatus(const std::string& model_run_id, 
                                      const std::string& status, 
                                      const std::string& artifact_path = "", 
                                      const std::string& error = "",
                                      const nlohmann::json& error_summary = nlohmann::json()) = 0;
    virtual nlohmann::json GetModelRun(const std::string& model_run_id) = 0;
    virtual nlohmann::json GetHpoTrials(const std::string& parent_run_id) = 0;
    virtual nlohmann::json GetHpoTrialsPaginated(const std::string& parent_run_id, int limit, int offset) = 0;
    virtual std::map<std::string, nlohmann::json> GetBulkHpoTrialSummaries(const std::vector<std::string>& parent_run_ids) = 0;
    virtual void UpdateBestTrial(const std::string& parent_run_id,
                                 const std::string& best_trial_run_id,
                                 double best_metric_value,
                                 const std::string& best_metric_name,
                                 const std::string& best_metric_direction,
                                 const std::string& tie_break_basis) = 0;

    virtual std::string CreateInferenceRun(const std::string& model_run_id) = 0;
    virtual void UpdateInferenceRunStatus(const std::string& inference_id, 
                                          const std::string& status, 
                                          int anomaly_count, 
                                          const nlohmann::json& details = {},
                                          double latency_ms = 0.0) = 0;
    
    virtual void UpdateTrialEligibility(const std::string& model_run_id,
                                        bool is_eligible,
                                        const std::string& reason,
                                        double metric_value,
                                        const std::string& source = "") = 0;

    virtual void UpdateParentErrorAggregates(const std::string& parent_run_id,
                                             const nlohmann::json& error_aggregates) = 0;

    virtual void InsertDatasetScores(const std::string& dataset_id,
                                     const std::string& model_run_id,
                                     const std::vector<std::pair<long, std::pair<double, bool>>>& scores) = 0;

    virtual long GetDatasetRecordCount(const std::string& dataset_id) = 0;

    virtual nlohmann::json ListGenerationRuns(int limit,
                                              int offset,
                                              const std::string& status = "",
                                              const std::string& created_from = "",
                                              const std::string& created_to = "") = 0;
    virtual nlohmann::json GetDatasetDetail(const std::string& run_id) = 0;
    virtual nlohmann::json GetDatasetSamples(const std::string& run_id, int limit) = 0;
    virtual nlohmann::json GetDatasetSummary(const std::string& run_id, int topk) = 0;
    virtual nlohmann::json GetTopK(const std::string& run_id,
                                   const std::string& column,
                                   int k,
                                   const std::string& region,
                                   const std::string& is_anomaly,
                                   const std::string& anomaly_type,
                                   const std::string& start_time,
                                   const std::string& end_time,
                                   bool include_total_distinct = false) = 0;
    virtual nlohmann::json GetTimeSeries(const std::string& run_id,
                                         const std::vector<std::string>& metrics,
                                         const std::vector<std::string>& aggs,
                                         int bucket_seconds,
                                         const std::string& region,
                                         const std::string& is_anomaly,
                                         const std::string& anomaly_type,
                                         const std::string& start_time,
                                         const std::string& end_time) = 0;
    virtual nlohmann::json GetHistogram(const std::string& run_id,
                                        const std::string& metric,
                                        int bins,
                                        double min_val,
                                        double max_val,
                                        const std::string& region,
                                        const std::string& is_anomaly,
                                        const std::string& anomaly_type,
                                        const std::string& start_time,
                                        const std::string& end_time) = 0;
    virtual nlohmann::json SearchDatasetRecords(const std::string& run_id,
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
                                                const std::string& anchor_time) = 0;
    virtual nlohmann::json GetDatasetRecord(const std::string& run_id, long record_id) = 0;
    virtual nlohmann::json GetMetricStats(const std::string& run_id, const std::string& metric) = 0;
    virtual nlohmann::json GetDatasetMetricsSummary(const std::string& run_id) = 0;
    virtual nlohmann::json GetModelsForDataset(const std::string& dataset_id) = 0;
    virtual nlohmann::json ListModelRuns(int limit,
                                         int offset,
                                         const std::string& status = "",
                                         const std::string& dataset_id = "",
                                         const std::string& created_from = "",
                                         const std::string& created_to = "") = 0;
    virtual nlohmann::json GetScoredDatasetsForModel(const std::string& model_run_id) = 0;
    virtual nlohmann::json GetScores(const std::string& dataset_id,
                                     const std::string& model_run_id,
                                     int limit,
                                     int offset,
                                     bool only_anomalies,
                                     double min_score,
                                     double max_score) = 0;
    virtual nlohmann::json ListInferenceRuns(const std::string& dataset_id,
                                             const std::string& model_run_id,
                                             int limit,
                                             int offset,
                                             const std::string& status = "",
                                             const std::string& created_from = "",
                                             const std::string& created_to = "") = 0;
    virtual nlohmann::json GetInferenceRun(const std::string& inference_id) = 0;
    virtual nlohmann::json GetEvalMetrics(const std::string& dataset_id,
                                          const std::string& model_run_id,
                                          int points,
                                          int max_samples) = 0;
    virtual nlohmann::json GetErrorDistribution(const std::string& dataset_id,
                                                const std::string& model_run_id,
                                                const std::string& group_by) = 0;

    virtual std::string CreateScoreJob(const std::string& dataset_id, 
                                       const std::string& model_run_id,
                                       const std::string& request_id = "") = 0;
    virtual void UpdateScoreJob(const std::string& job_id,
                                const std::string& status,
                                long total_rows,
                                long processed_rows,
                                long last_record_id = 0,
                                const std::string& error = "") = 0;
    virtual nlohmann::json GetScoreJob(const std::string& job_id) = 0;
    virtual nlohmann::json ListScoreJobs(int limit,
                                         int offset,
                                         const std::string& status = "",
                                         const std::string& dataset_id = "",
                                         const std::string& model_run_id = "",
                                         const std::string& created_from = "",
                                         const std::string& created_to = "") = 0;

    struct ScoringRow {
        long record_id = 0;
        bool is_anomaly = false;
        double cpu = 0.0;
        double mem = 0.0;
        double disk = 0.0;
        double rx = 0.0;
        double tx = 0.0;
    };
    virtual std::vector<ScoringRow> FetchScoringRowsAfterRecord(const std::string& dataset_id,
                                                                long last_record_id,
                                                                int limit) = 0;
};
